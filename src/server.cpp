#include "server.hpp"
#include "logger.hpp"
#include "server_config.hpp"
#include "server_utils.hpp"

#include <event.h>
#include <event2/http.h>
#include <evhttp.h>
#include <fcntl.h>
#include <array>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include "base64.hpp"

namespace ricox {

// Utility functions
static auto to_hex(uint8_t x) -> uint8_t { return x + (x < 10 ? '0' : 'a' - 10); }

static auto from_hex(uint8_t x) -> uint8_t {
	return x - (x <= '9' ? '0' : (x >= 'a' ? 'a' - 10 : (x >= 'A' ? 'A' - 10 : 0)));
}

static auto url_decode(const std::string& str) -> std::string {
	auto result = std::string{};
	result.reserve(str.size());

	for (auto i = 0; i < str.size(); ++i) {
		if (str[i] == '%' && i + 2 < str.size() && isxdigit(str[i + 1]) && isxdigit(str[i + 2])) {
			result += static_cast<char>((from_hex(str[i + 1]) << 4) | from_hex(str[i + 2]));
			i += 2;
		} else if (str[i] == '+')
			result += ' ';
		else
			result += str[i];
	}
	return result;
}

server::server() {
	server_port = server_config::get_instance().get_server_port();
	server_ip = server_config::get_instance().get_server_ip();
	download_url_prefix = server_config::get_instance().get_download_url_prefix();
}

// static functions of the class
auto server::generic_callback(evhttp_request* req, void* arg) -> void {
	auto path = std::string{evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req))};
	path = std::move(url_decode(path));

	common::INFO("server_logger", "Generic Request: URI: {}", path.c_str());

	if (path.find("/download") != std::string::npos) {
		// Download
		server::download(req, arg);
	} else if (path == "/upload") {
		// Upload
		server::upload(req, arg);
	} else if (path == "/") {
		// Display list of files
		server::show(req, arg);
	} else {
		evhttp_send_reply(req, HTTP_NOTIMPLEMENTED, "Request not implemented", nullptr);
	}
}

auto server::download(evhttp_request* req, void* arg) -> void {
	// get the path from the request
	auto path = std::string{evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req))};
	path = std::move(url_decode(path));

	// get the storage_info from the path
	auto info = storage_info{};
	data_manager::get_instance().find_by_url(path, info);

	auto download_path = info.file_path;
	if (download_path.find(server_config::get_instance().get_hot_storage_path()) == std::string::npos) {
		// The file is compressed in cold storage, not available in hot storage
		// The file needs decompression first
		common::INFO("server_logger", "Decompressing file at: {}", download_path.c_str());
		auto file = file_util{download_path};
		download_path = server_config::get_instance().get_hot_storage_path() +
						std::string{download_path.begin() + download_path.find_last_of('/') + 1, download_path.end()};

		auto new_dir = file_util{server_config::get_instance().get_hot_storage_path()};
		if (!new_dir.create_directory()) {
			common::ERROR("server_logger", "Failed to create directory for download: {}",
						  server_config::get_instance().get_hot_storage_path());
			return;
		}

		file.decompress(download_path);	 // Decompress file to hot_storage for download
	}

	// The file is available in hot storage
	common::INFO("server_logger", "Download requested at: {}", download_path.c_str());
	auto file = file_util{download_path};
	if (!file.exists() && info.file_path.find("storage/cold") != std::string::npos) {
		// Decompression from cold storage failed
		common::ERROR("server_logger", "Server decompression error, sending 500");
		evhttp_send_reply(req, HTTP_INTERNAL, "Decompression failed", nullptr);
		return;
	} else if (!file.exists() && info.file_path.find("storage/cold") != std::string::npos) {
		// User requested a wrong file from hot storage, and the file does not exist
		common::ERROR("server_logger", "User's bad request for non-existent file, sending 400");
		evhttp_send_reply(req, HTTP_BADREQUEST, "File non-existent", nullptr);
		return;
	}

	// If the file has been sent but not complete, use RE-TRANS
	auto retrans = false;
	auto old_etag = std::string{};
	auto if_range = evhttp_find_header(req->input_headers, "If-Range");

	if (if_range) {
		old_etag = std::string{if_range};
		if (old_etag == get_etag(info)) {  // if the etag is still up to date, then eligible for re-transmission
			common::INFO("server_logger", "File {} eligible for resume transmission from breakpoint",
						 download_path.c_str());
			retrans = true;
		}
	}

	// Load the file into the response to client
	if (!file.exists()) {
		common::ERROR("server_logger", "Unknown error, file does not exist at {}", download_path.c_str());
		evhttp_send_reply(req, HTTP_NOTFOUND, "File non-existent, unknown error", nullptr);
		return;
	}

	auto output_buffer = evhttp_request_get_output_buffer(req);
	auto fd = open(download_path.c_str(), O_RDONLY);  // Use C-style file descriptor to be compatible with libevent
	if (fd < 0) {
		common::ERROR("server_logger", "Unable to create file descriptor for file: {}", download_path.c_str());
		evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), nullptr);
		return;
	}

	if (evbuffer_add_file(output_buffer, fd, 0, file.get_file_size()) == -1) {
		common::ERROR("server_logger", "Unable to load file {} to buffer", download_path.c_str());
		evhttp_send_reply(req, HTTP_INTERNAL, "Cannot add file to response buffer", nullptr);
		return;
	}

	// Build response header with ETag, Accept-ranges: bytes
	evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");
	evhttp_add_header(req->output_headers, "ETag", get_etag(info).c_str());
	evhttp_add_header(req->output_headers, "Content-Type", "application/octet-stream");

	if (retrans) {	// retrans uses 206
		common::INFO("server_logger", "Sending response 206 [Retransmission] : {}", download_path.c_str());
		evhttp_send_reply(req, 206, "Resume transmission from breakpoint", nullptr);
	} else {  // Normal transmission, no retrans
		common::INFO("server_logger", "Sending response 200 [Transmission] : {}", download_path.c_str());
		evhttp_send_reply(req, HTTP_OK, "Success", nullptr);
	}

	if (download_path != info.file_path) {
		std::remove(download_path.c_str());
	}
}

auto server::upload(evhttp_request* req, void* arg) -> void {
	// Hot storage: directly store
	// Cold storage: first compress then store

	auto input_buffer = evhttp_request_get_input_buffer(req);
	if (!input_buffer) {
		common::ERROR("server_logger", "Failed to get input buffer");
		evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid request (invalid input buffer)", nullptr);
		return;
	}

	auto buffer_size = evbuffer_get_length(input_buffer);
	if (!buffer_size) {
		common::ERROR("server_logger", "Uploading an empty file");
		evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid request (empty file)", nullptr);
		return;
	}

	auto data = std::string(buffer_size, '\0');
	if (evbuffer_copyout(input_buffer, reinterpret_cast<void*>(data.data()), buffer_size) == -1) {
		common::ERROR("server_logger", "Failed to copy from input buffer");
		evhttp_send_reply(req, HTTP_INTERNAL, "Unable to copy from uploaded file", nullptr);
		return;
	}

	auto file_name = std::string{evhttp_find_header(req->input_headers, "FileName")};
	file_name = std::move(base64_decode(file_name));

	auto storage_type = std::string{evhttp_find_header(req->input_headers, "StorageType")};
	auto storage_path = std::string{};

	if (storage_type == "hot") {
		storage_path = server_config::get_instance().get_hot_storage_path();
	} else if (storage_type == "cold") {
		storage_path = server_config::get_instance().get_cold_storage_path();
	} else {
		common::ERROR("server_logger", "Invalid storage type specified by user");
		evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid storage type", nullptr);
		return;
	}

	auto new_dir = file_util{storage_path};	 // Create directory for storage if not exist
	if (!new_dir.create_directory()) {
		common::ERROR("server_logger", "Failed to create directory for upload: {}", storage_path.c_str());
		evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot create storage directory", nullptr);
		return;
	}

	storage_path += "/" + file_name;
	auto file = file_util{storage_path};  // Create file util object for the new file
	if (storage_type == "cold") {
		// Cold storage: compress first
		if (!file.compress(data, server_config::get_instance().get_bundle_type())) {
			common::ERROR("server_logger", "Failed to compress file for cold storage");
			evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot compress file for cold storage", nullptr);
			return;
		}
	} else {
		// Hot storage: directly write
		if (!file.write_file(data)) {
			common::ERROR("server_logger", "Failed to write file for hot storage");
			evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot write file for hot storage", nullptr);
			return;
		}
	}

	// Add storage info to data manager
	auto info = storage_info{storage_path};
	if (!data_manager::get_instance().add_info(info)) {
		common::ERROR("server_logger", "Failed to add storage info to data manager");
		evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot update storage info", nullptr);
		return;
	}

	common::INFO("server_logger", "File {} uploaded successfully to {}", file_name.c_str(), storage_path.c_str());
	evhttp_send_reply(req, HTTP_OK, "File uploaded successfully", nullptr);
}

auto server::show(evhttp_request* req, void* arg) -> void {
	auto files = std::vector<storage_info>{};
	if (!data_manager::get_instance().find_all(files)) {
		common::ERROR("server_logger", "Failed to retrieve file list from data manager");
		evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot retrieve file list", nullptr);
		return;
	}

	auto ifs = std::ifstream{"./static/index.html"};
	auto html_template = std::string{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};

	// Replace {{FILE_LIST}} in html_template with generated file list
	html_template = std::regex_replace(html_template, std::regex(R"(\{\{FILE_LIST\}\})"), generate_file_list(files));
	// Replace {{BACKEND_URL}} in html_template with Backend URL from config
	html_template = std::regex_replace(html_template, std::regex(R"(\{\{BACKEND_URL\}\})"),
									   "http://" + server_config::get_instance().get_server_ip() + ":" +
										   std::to_string(server_config::get_instance().get_server_port()));

	auto output_buffer = evhttp_request_get_output_buffer(req);
	if (evbuffer_add(output_buffer, reinterpret_cast<const void*>(html_template.c_str()), html_template.size()) == -1) {
		common::ERROR("server_logger", "Failed to add HTML content to output buffer");
		evhttp_send_reply(req, HTTP_INTERNAL, "Server error: cannot prepare HTML content", nullptr);
		return;
	}

	evhttp_add_header(req->output_headers, "Content-Type", "text/html; charset=UTF-8");
	evhttp_send_reply(req, HTTP_OK, "Success", nullptr);
}

auto server::generate_file_list(const std::vector<storage_info>& files) -> std::string {
	// Generate text in HTML format to display the files
	auto ss = std::stringstream{};
	ss << "<div class='file-list'><h3>Uploaded Files</h3>";

	for (const auto& file : files) {
		auto file_name = file_util{file.file_path}.get_file_name();
		auto is_cold = file.file_path.find("storage/cold") != std::string::npos ? true : false;

		ss << "<div class='file-item'>"
		   << "<div class='file-info'>"
		   << "<span>📄" << file_name << "</span>"
		   << "<span class='file-type'>" << (is_cold ? "Cold Storage" : "Hot Storage") << "</span>"
		   << "<span>" << format_size(file.file_size) << "</span>"
		   << "<span>" << std::ctime(&file.time_modified) << "</span>"
		   << "</div>"
		   << "<button onclick=\"window.location='" << file.file_url << "'\">⬇️ Download</button>"
		   << "</div>";
	}

	ss << "</div>";
	return ss.str();
}

auto server::format_size(uint64_t bytes) -> std::string {
	static auto units = std::array<std::string, 4>{"B", "kB", "MB", "GB"};
	auto idx = 0;

	while (bytes >= 1024 && idx < 3) {
		bytes /= 1024;
		++idx;
	}

	return std::to_string(bytes) + " " + units[idx];
}

auto server::get_etag(const storage_info& info) -> std::string {
	// Format: NAME-SIZE-TIME_MODIFIED
	auto file = file_util{info.file_path};
	auto etag = file.get_file_name();
	etag += "-";
	etag += std::to_string(info.file_size);
	etag += std::to_string(info.time_modified);

	return etag;
}

// non static functions
auto server::start_server() -> bool {
	auto base = event_base_new();
	if (!base) {
		common::ERROR("server_logger", "Cannot create event base");
		return false;
	}

	auto httpd = evhttp_new(base);
	if (!httpd) {
		common::ERROR("server_logger", "Cannot create httpd");
	}

	auto sin = sockaddr_in{};
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;  // IPv4
	sin.sin_port = htons(server_port);

	if (evhttp_bind_socket(httpd, "0.0.0.0", server_port) != 0) {
		common::ERROR("server_logger", "Cannot bind httpd to socket");
		return false;
	}

	// Set generic callback function (not specific to URL)
	evhttp_set_gencb(httpd, generic_callback, nullptr);

	if (event_base_dispatch(base) == -1) {
		common::ERROR("server_logger", "Event base failed to dispatch");
		return false;
	}

	// C Style resource deallocation
	event_base_free(base);
	evhttp_free(httpd);

	return true;
}

}  // namespace ricox