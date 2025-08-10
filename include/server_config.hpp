#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace ricox {
static constexpr char* CONFIG_FILE = "../conf/storage_server.json";

class server_config final {	 // Singleton class to manage server configuration
   private:
	static std::mutex config_mutex;

	int server_port;
	std::string server_ip;
	std::string download_url_prefix;
	std::string cold_storage_path;
	std::string hot_storage_path;
	std::string storage_info;
	int bundle_type;  // Compression format

	server_config();
	server_config(const server_config&) = delete;
	server_config& operator=(const server_config&) = delete;
	server_config(server_config&&) = delete;
	server_config& operator=(server_config&&) = delete;

   public:
    static auto get_instance() -> server_config&;
    auto load_config() -> bool;
    auto get_server_port() const -> int;
    auto get_server_ip() const -> const std::string&;
    auto get_download_url_prefix() const -> const std::string&;
    auto get_cold_storage_path() const -> const std::string&;
    auto get_hot_storage_path() const -> const std::string&;
    auto get_storage_info() const -> const std::string&;
    auto get_bundle_type() const -> int;
};

}  // namespace ricox