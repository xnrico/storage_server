#include "server_utils.hpp"
#include "bundle.hpp"
#include "logger.hpp"

#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ricox {
auto file_util::get_file_name() const -> const std::string {
	auto pos = file_name.find_last_of("/");
	if (pos != std::string::npos) return file_name.substr(pos + 1);
	return file_name;
}

auto file_util::get_file_size() const -> int64_t {
	try {
		return fs::file_size(file_name);
	} catch (const fs::filesystem_error& e) {
		common::ERROR("server_logger", "Error getting file size for {}: {}", file_name, e.what());
		return -1;
	}
}

auto file_util::get_last_access_time() const -> std::time_t {
	// std::filesystem does not provide direct access to last access time
	struct stat file_stat;
	auto ret = stat(file_name.c_str(), &file_stat);
	if (ret != 0) {
		common::ERROR("server_logger", "Error getting last access time for {}: {}", file_name, strerror(errno));
		return -1;
	}
	return file_stat.st_atime;
}

auto file_util::get_last_write_time() const -> std::time_t {
	// Use std::filesystem to get last write time
	try {
		auto ftime = fs::last_write_time(file_name);
		// Convert file_time_type to system_clock::time_point (C++20 feature)
		// Note: This conversion is implementation-defined, so it may vary across platforms.
		auto system_time_point = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
		// Convert system_clock::time_point to time_t
		auto cftime = std::chrono::system_clock::to_time_t(system_time_point);
		return cftime;
	} catch (const fs::filesystem_error& e) {
		common::ERROR("server_logger", "Error getting last write time for {}: {}", file_name, e.what());
		return -1;
	}
}

auto file_util::read_content(std::string& content, size_t pos, size_t len) const -> bool {
	if (pos + len > get_file_size()) {
		common::ERROR("server_logger", "Read exceeds the size of file {}", get_file_name().c_str());
		return false;
	}

	auto ifs = std::ifstream{};
	ifs.open(file_name.c_str(), std::ios::binary);	// open in binary mode
	if (!ifs.is_open()) {
		common::ERROR("server_logger", "Unable to open file {}", get_file_name().c_str());
		return false;
	}

	ifs.seekg(pos, std::ios::beg);	// offset read position from beginning
	content.resize(len);
	ifs.read(content.data(), len);
	if (!ifs.good()) {	// check for error flags if any
		common::ERROR("server_logger", "Read file content error {}", get_file_name().c_str());
		ifs.close();
		return false;
	}

	ifs.close();
	return true;
}

auto file_util::read_file(std::string& content) const -> bool { return read_content(content, 0, get_file_size()); }

auto file_util::write_content(const std::string& content, size_t len) const -> bool {
	auto ofs = std::ofstream{};
	ofs.open(file_name, std::ios::binary);	// open in binary mode

	if (!ofs.is_open()) {
		common::ERROR("server_logger", "Unable to open file {}", get_file_name().c_str());
		return false;
	}

	ofs.write(content.c_str(), len);

	if (!ofs.good()) {	// check for error flags if any
		common::ERROR("server_logger", "Write file content error {}", get_file_name().c_str());
		ofs.close();
		return false;
	}

	ofs.close();
	return true;
}

auto file_util::write_file(const std::string& content) const -> bool { return write_content(content, content.size()); }

auto file_util::compress(const std::string& content, int format) const -> bool {
	auto compressed_data = bundle::pack(format, content);

	if (compressed_data.size() == 0) {
		common::ERROR("server_logger", "Invalid archive size: {}", get_file_name().c_str());
		return false;
	}

	auto compressed_file = file_util(file_name);  // temp object, overrides the file with compressed data
	return compressed_file.write_content(compressed_data.c_str(), compressed_data.size());
}

auto file_util::decompress(const std::string& download_path) const -> bool {
	auto compressed_data = std::string{};
	if (!read_file(compressed_data)) {
		common::ERROR("server_logger", "Cannot decompress data of file: {}", get_file_name().c_str());
		return false;
	}

	auto decompressed_data = bundle::unpack(compressed_data);
	auto decompressed_file = file_util(download_path);
	return decompressed_file.write_content(decompressed_data.c_str(), decompressed_data.size());
}

auto file_util::exists() const -> bool { return fs::exists(file_name); }

auto file_util::create_directory() const -> bool {
	if (exists()) return true;
	return fs::create_directories(file_name);
}

auto file_util::scan_directory(std::vector<std::string>& files) const -> bool {
	if (!fs::exists(file_name) || !fs::is_directory(file_name)) {
		common::ERROR("server_logger", "Invalid directory path: {}", file_name.c_str());
		return false;
	}

	for (const auto& entry : fs::directory_iterator(file_name)) {
		if (entry.is_regular_file()) {
			files.push_back(entry.path().relative_path().string());
		}
	}

	return true;
}

auto json_util::serialize(const Json::Value& json_val, std::string& str) -> bool {
	auto swb = Json::StreamWriterBuilder{};
	swb["emitUTF8"] = true;	 // Ensure UTF-8 encoding

	auto writer = std::unique_ptr<Json::StreamWriter>(swb.newStreamWriter());
	auto ss = std::stringstream{};

	if (!writer->write(json_val, &ss)) {
		common::ERROR("server_logger", "Failed to serialize JSON value");
		return false;
	}

	str = std::move(ss.str());
	return true;
}

auto json_util::deserialize(Json::Value& json_val, const std::string& str) -> bool {
	auto crb = Json::CharReaderBuilder{};
	auto reader = std::unique_ptr<Json::CharReader>(crb.newCharReader());

	std::string errors;

	if (!reader->parse(str.c_str(), str.c_str() + str.size(), &json_val, &errors)) {
		common::ERROR("server_logger", "Failed to deserialize JSON value: {}", errors);
		return false;
	}

	return true;
}
}  // namespace ricox
