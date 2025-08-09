#include "server_config.hpp"
#include "logger.hpp"

namespace ricox {
std::mutex server_config::config_mutex;

server_config::server_config() {
    if (!load_config()) {
        common::FATAL("server_logger", "Failed to load server configuration from {}", CONFIG_FILE);
        return;
    }

    common::INFO("server_logger", "Server configuration loaded successfully from {}", CONFIG_FILE);
}


auto server_config::get_instance() -> server_config& {
	static auto config = server_config{};
	return config;
}

auto server_config::load_config() -> bool {
    // Load configuration from file
	common::get_logger("server_logger")->info("Loading server configuration from {}", CONFIG_FILE);
	

	return true;
}

auto server_config::get_server_port() const -> int { return server_port; }

auto server_config::get_server_ip() const -> const std::string& { return server_ip; }

auto server_config::get_download_url_prefix() const -> const std::string& { return download_url_prefix; }

auto server_config::get_cold_storage_path() const -> const std::string& { return cold_storage_path; }

auto server_config::get_hot_storage_path() const -> const std::string& { return hot_storage_path; }

auto server_config::get_storage_info() const -> const std::string& { return storage_info; }

auto server_config::get_bundle_type() const -> int { return bundle_type; }

}  // namespace ricox
