#pragma once
#include <string>
#include <unordered_map>
#include "server_config.hpp"
#include <shared_mutex>

namespace ricox {
struct storage_info final {
   public:
	std::time_t time_modified;
	std::time_t time_accessed;
	size_t file_size;
	std::string file_path;
	std::string file_url;

	storage_info() = default;
	~storage_info() = default;
	storage_info(const std::string& path);
	auto load_info(const std::string& path) -> bool;
};

class data_manager final {
   private:
	std::string storage_file;
	std::unordered_map<std::string, storage_info> storage_map;	// key: file name, value: storage info
    mutable std::shared_mutex mutex;
    bool is_cold_storage;

    auto add_info(const storage_info& info) -> bool;
    auto store_info() -> bool;

   public:
	data_manager();
	~data_manager() = default;

    data_manager(const data_manager&) = delete;
    data_manager& operator=(const data_manager&) = delete;

	auto initialize() -> bool;
    auto update(const storage_info& info) -> bool;
    auto find_by_url(const std::string& url, storage_info& info) const -> bool;
    auto find_by_path(const std::string& path, storage_info& info) const -> bool;
    auto find_all(std::vector<storage_info>& infos) const -> bool;
};

}  // namespace ricox