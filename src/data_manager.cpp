#include "data_manager.hpp"
#include "logger.hpp"
#include "server_config.hpp"
#include "server_utils.hpp"

#include <vector>

namespace ricox {
storage_info::storage_info(const std::string& path) { load_info(path); }

auto storage_info::load_info(const std::string& path) -> bool {
	auto file = file_util{path};
	if (!file.exists()) {
		common::ERROR("server_logger", "File does not exist: {}", path);
		return false;
	}

	time_modified = file.get_last_write_time();
	time_accessed = file.get_last_access_time();
	file_size = static_cast<size_t>(file.get_file_size());
	file_path = path;
	file_url = server_config::get_instance().get_download_url_prefix() + "/" + file.get_file_name();

	common::INFO("server_logger", "Loaded storage info for file: {}", path);
	common::INFO("server_logger", "URL: {}, Last Modified: {}, Last Accessed: {}, Size: {}", file_url, time_modified,
				 time_accessed, file_size);

	return true;
}

data_manager::data_manager() : storage_file{server_config::get_instance().get_storage_info()}, is_cold_storage{false} {}

auto data_manager::add_info(const storage_info& info) -> bool {
	auto lock = std::unique_lock{mutex};
	storage_map.try_emplace(info.file_url, info);
	if (is_cold_storage && !store_info()) {
		common::ERROR("server_logger", "Failed to store storage info for cold storage");
		return false;
	}

	return true;
}

auto data_manager::store_info() -> bool {
	auto infos = std::vector<storage_info>{};
	if (!find_all(infos)) {
		common::ERROR("server_logger", "Failed to find all storage info (Maybe empty map)");
		return false;
	}

	auto root = Json::Value{};

	// Prepare JSON array to hold storage info
	for (const auto& info : infos) {
		auto item = Json::Value{};
		item["time_modified"] = static_cast<Json::Int64>(info.time_modified);
		item["time_accessed"] = static_cast<Json::Int64>(info.time_accessed);
		item["file_size"] = static_cast<Json::UInt64>(info.file_size);
		item["file_path"] = info.file_path.c_str();
		item["file_url"] = info.file_url.c_str();
		root.append(item);
	}

	// Serialize JSON to string
	auto json_str = std::string{};
	if (!json_util::serialize(root, json_str)) {
		common::ERROR("server_logger", "Failed to serialize storage info to JSON");
		return false;
	}

	// Write JSON string to file
	auto file = file_util{storage_file};
	if (!file.write_file(json_str)) {
		common::ERROR("server_logger", "Failed to write storage info to file: {}", storage_file);
		return false;
	}

	return true;
}

auto data_manager::initialize() -> bool {
	auto file = file_util{storage_file};
	if (!file.exists()) {
		common::ERROR("server_logger", "Storage info file does not exist: {}", storage_file);
		return true;
	}

	auto body = std::string{};
	if (!file.read_file(body)) {
		common::ERROR("server_logger", "Failed to read storage info file: {}", storage_file);
		return false;
	}

	auto root = Json::Value{};
	if (!json_util::deserialize(root, body)) {
		common::ERROR("server_logger", "Failed to parse storage info JSON");
		return false;
	}

	for (const auto& item : root) {
		auto file_info = storage_info{};
		file_info.time_modified = item["time_modified"].asInt64();
		file_info.time_accessed = item["time_accessed"].asInt64();
		file_info.file_size = item["file_size"].asUInt64();
		file_info.file_path = item["file_path"].asString();
		file_info.file_url = item["file_url"].asString();

		if (!add_info(file_info)) {
			common::ERROR("server_logger", "Failed to add storage info for file: {}", file_info.file_path);
			return false;
		}
	}

	common::INFO("server_logger", "Initialized data manager with {} storage entries", storage_map.size());
	return true;
}

auto data_manager::update(const storage_info& info) -> bool {
	{
		auto lock = std::unique_lock{mutex};
		storage_map[info.file_url] = info;
	}

	if (!store_info()) {
		common::ERROR("server_logger", "Failed to update storage info for file: {}", info.file_path);
		return false;
	}

	return true;
}

auto data_manager::find_by_url(const std::string& url, storage_info& info) const -> bool {
	auto lock = std::shared_lock{mutex};
	auto it = storage_map.find(url);
	if (it != storage_map.end()) {
		info = it->second;
		return true;
	}

	return false;
}

auto data_manager::find_by_path(const std::string& path, storage_info& info) const -> bool {
	auto lock = std::shared_lock{mutex};
	for (const auto& [_, storage_info] : storage_map) {
		if (storage_info.file_path == path) {
			info = storage_info;
			return true;
		}
	}

	return false;
}

auto data_manager::find_all(std::vector<storage_info>& infos) const -> bool {
	auto lock = std::shared_lock{mutex};
	infos.clear();
	for (const auto& [_, info] : storage_map) {
		infos.emplace_back(info);
	}

	return !infos.empty();
}

}  // namespace ricox