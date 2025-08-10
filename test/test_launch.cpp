#include "server.hpp"
#include "logger.hpp"
#include <thread>


auto main(int argc, char* argv[]) -> int {
    // Initialize logger
    auto server_logger = ricox::common::create_logger("server_logger", {std::make_shared<ricox::std_flush>()});

    auto server_thread = std::thread([]() -> void {
        auto server_instance = ricox::server{};
        
        ricox::common::INFO("server_logger", "Starting server...");

        if (!server_instance.start_server()) {
            ricox::common::ERROR("server_logger", "Server failed to start");
            return;
        }

        ricox::common::INFO("server_logger", "Server stopped.");
    });

    if (server_thread.joinable()) {
        server_thread.join();
    }

    return 0;
}