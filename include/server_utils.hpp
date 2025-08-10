#pragma once

#include <jsoncpp/json/json.h>
#include <filesystem>
#include <vector>

namespace ricox {
namespace fs = std::filesystem;

class file_util final {
   private:
	std::string file_name;

   public:
	file_util(const std::string& name) : file_name(name) {}

	auto get_file_name() const -> const std::string;
	auto get_file_size() const -> int64_t;
	auto get_last_access_time() const -> std::time_t;
	auto get_last_write_time() const -> std::time_t;

	auto read_file(std::string& content) const -> bool;								// reads entire file
	auto read_content(std::string& content, size_t pos, size_t len) const -> bool;	// reads part of file
	auto write_file(const std::string& content) const -> bool;						// writes entire content buffer
	auto write_content(const std::string& content, size_t len) const -> bool;		// writes part of content buffer
	auto compress(const std::string& content, int format) const -> bool;
	auto decompress(const std::string& download_path) const -> bool;

	auto exists() const -> bool;
	auto create_directory() const -> bool;
	auto scan_directory(std::vector<std::string>& files) const -> bool;
};

struct json_util final {
   public:
	static auto serialize(const Json::Value& json_val, std::string& str) -> bool;
	static auto deserialize(Json::Value& json_val, const std::string& str) -> bool;
};

}  // namespace ricox