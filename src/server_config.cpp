#include "server_config.hpp"
#include "server_utils.hpp"
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
	common::INFO("server_logger", "Loading server configuration from {}", CONFIG_FILE);
    auto file = file_util{CONFIG_FILE};
    auto content = std::string{};
    if (!file.read_file(content)) {
        common::ERROR("server_logger", "Failed to read configuration file {}", CONFIG_FILE);
        return false;
    }

    auto root = Json::Value{};
    if (!json_util::deserialize(root, content)) {
        common::ERROR("server_logger", "Failed to parse JSON from configuration file {}", CONFIG_FILE);
        return false;
    }

    // Extract values from the JSON object
    server_port = root.get("server_port", 8081).asInt();
    server_ip = root.get("server_ip", "127.0.0.1").asString();
    download_url_prefix = root.get("download_url_prefix", "/downloads").asString();
    cold_storage_path = root.get("cold_storage_path", "./storage/cold").asString();
    hot_storage_path = root.get("hot_storage_path", "./storage/hot").asString();
    storage_info = root.get("storage_info", "./default_storage").asString();
    bundle_type = root.get("bundle_type", 4).asInt();  // Default to 4 if not specified

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
