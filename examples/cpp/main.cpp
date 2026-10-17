#include <communicationdriver/communicationdriver.hpp>
#include <iostream>

int main()
{
    comm::Driver driver;
    comm::CliOptions options;
    options.execution_mode = comm::ExecutionMode::OneShot;

    auto connection = driver.create_cli(options);
    connection.open();
#ifdef _WIN32
    auto result = connection.execute("echo READY", comm::Check::contains("READY"));
#else
    auto result = connection.execute("printf READY", comm::Check::contains("READY"));
#endif
    std::cout << result.response << "\npassed=" << result.check->passed << "\n";
    connection.close();
}
