#include <communicationdriver/communicationdriver.h>
#include <stdio.h>

int main(void)
{
    cd_driver_t* driver = NULL;
    cd_connection_t* connection = NULL;
    cd_result_t* result = NULL;
    cd_cli_options_t options;
    cd_cli_options_init(&options);
    options.execution_mode = CD_ONE_SHOT;

    if (cd_driver_create(&driver) != CD_OK ||
        cd_create_cli(driver, &options, &connection, &result) != CD_OK)
        return 1;
    if (cd_open(connection, NULL, &result) != CD_OK)
        return 2;

    cd_result_release(result);

    cd_check_t check = cd_check_contains_string(CD_SV("READY"));

    if (cd_execute_string(connection, CD_SV("printf READY"), &check, NULL, &result) != CD_OK)
        return 3;

    cd_string_view_t response;
    cd_check_result_t checked = {0};
    checked.struct_size = sizeof(checked);
    cd_result_get_text(result, CD_STREAM_STDOUT, &response);
    cd_result_get_check(result, &checked);
    fwrite(response.data, 1, (size_t)response.size, stdout);
    printf("\npassed=%d\n", checked.passed);
    cd_result_release(result);
    cd_connection_release(connection);
    cd_driver_release(driver);

    return 0;
}
