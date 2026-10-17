#ifndef COMMUNICATIONDRIVER_H
#define COMMUNICATIONDRIVER_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
#if defined(CD_BUILDING)
#define CD_API __declspec(dllexport)
#else
#define CD_API __declspec(dllimport)
#endif
#define CD_CALL __cdecl
#else
#define CD_API __attribute__((visibility("default")))
#define CD_CALL
#endif
#ifdef __cplusplus
extern "C"
{
#endif
#define CD_ABI_VERSION 1u
#define CD_INFINITE UINT64_MAX
    typedef struct cd_driver cd_driver_t;
    typedef struct cd_connection cd_connection_t;
    typedef struct cd_operation cd_operation_t;
    typedef struct cd_result cd_result_t;
    typedef struct cd_cancel_source cd_cancel_source_t;
    typedef int32_t cd_status_t;

    enum
    {
        CD_OK,
        CD_INVALID_ARGUMENT,
        CD_NOT_SUPPORTED,
        CD_NOT_OPEN,
        CD_CLOSED,
        CD_TIMEOUT,
        CD_CANCELLED,
        CD_CONNECTION_FAILED,
        CD_DISCONNECTED,
        CD_END_OF_STREAM,
        CD_AUTHENTICATION_FAILED,
        CD_HOST_KEY_REJECTED,
        CD_DEVICE_UNAVAILABLE,
        CD_PERMISSION_DENIED,
        CD_IO_ERROR,
        CD_PROTOCOL_ERROR,
        CD_ENCODING_ERROR,
        CD_LIMIT_EXCEEDED,
        CD_BUSY,
        CD_NEEDS_RECOVERY,
        CD_INVALID_CONTEXT,
        CD_INTERNAL_ERROR
    };

    typedef int32_t cd_state_t;

    enum
    {
        CD_STATE_CREATED,
        CD_STATE_OPENING,
        CD_STATE_OPEN,
        CD_STATE_CLOSING,
        CD_STATE_CLOSED,
        CD_STATE_FAULTED,
        CD_STATE_NEEDS_RECOVERY
    };

    typedef int32_t cd_encoding_t;

    enum
    {
        CD_ENCODING_DEFAULT,
        CD_ENCODING_UTF8,
        CD_ENCODING_ASCII,
        CD_ENCODING_GB18030
    };

    typedef int32_t cd_stream_t;

    enum
    {
        CD_STREAM_STDOUT,
        CD_STREAM_STDERR
    };

    typedef int32_t cd_execution_mode_t;

    enum
    {
        CD_PERSISTENT,
        CD_ONE_SHOT
    };

    typedef int32_t cd_shell_kind_t;

    enum
    {
        CD_SHELL_DEFAULT,
        CD_SHELL_CMD,
        CD_SHELL_WINDOWS_POWERSHELL,
        CD_SHELL_PWSH,
        CD_SHELL_SH,
        CD_SHELL_BASH,
        CD_SHELL_ZSH
    };

    typedef int32_t cd_completion_reason_t;

    enum
    {
        CD_COMPLETION_NONE,
        CD_COMPLETION_COMMAND,
        CD_COMPLETION_PROTOCOL,
        CD_COMPLETION_IDLE,
        CD_COMPLETION_MATCH
    };

    typedef int32_t cd_result_kind_t;

    enum
    {
        CD_RESULT_VOID,
        CD_RESULT_TEXT,
        CD_RESULT_BYTES,
        CD_RESULT_EXECUTION_TEXT,
        CD_RESULT_EXECUTION_BYTES,
        CD_RESULT_TRANSFER,
        CD_RESULT_BATCH,
        CD_RESULT_CHECK
    };

    typedef uint64_t cd_capabilities_t;

    enum
    {
        CD_CAP_READ = 1,
        CD_CAP_WRITE = 2,
        CD_CAP_EXECUTE = 4,
        CD_CAP_EXECUTE_UNTIL = 8,
        CD_CAP_CLOSE_INPUT = 16,
        CD_CAP_STDERR = 32,
        CD_CAP_UPLOAD = 64,
        CD_CAP_DOWNLOAD = 128,
        CD_CAP_MESSAGE = 256
    };

    typedef struct
    {
        const char* data;
        uint64_t size;
    } cd_string_view_t;

    typedef struct
    {
        const uint8_t* data;
        uint64_t size;
    } cd_bytes_view_t;
#ifdef __cplusplus
#define CD_SV(s) cd_string_view_t{(s), sizeof(s) - 1}
#else
#define CD_SV(s) ((cd_string_view_t){(s), sizeof(s) - 1})
#endif
    typedef int32_t cd_check_kind_t;

    enum
    {
        CD_CHECK_NONE,
        CD_CHECK_CONTAINS,
        CD_CHECK_EQUALS,
        CD_CHECK_REGEX
    };

    typedef struct
    {
        uint32_t struct_size;
        cd_check_kind_t kind;
        int32_t is_text;
        int32_t reserved;
        cd_string_view_t text;
        cd_bytes_view_t bytes;
    } cd_check_t;

    typedef int32_t cd_receive_kind_t;

    enum
    {
        CD_RECEIVE_NONE,
        CD_RECEIVE_BYTES,
        CD_RECEIVE_CHARACTERS,
        CD_RECEIVE_DELIMITER,
        CD_RECEIVE_PATTERN,
        CD_RECEIVE_IDLE,
        CD_RECEIVE_MESSAGE,
        CD_RECEIVE_AVAILABLE
    };

    typedef struct
    {
        uint32_t struct_size;
        cd_receive_kind_t kind;
        uint64_t count;
        cd_string_view_t text;
        cd_bytes_view_t bytes;
        int32_t is_text;
        int32_t include_delimiter;
        uint64_t idle_ms;
    } cd_receive_until_t;

    typedef struct
    {
        uint32_t struct_size;
        cd_encoding_t encoding;
        cd_string_view_t new_line;
        cd_receive_until_t receive_until;
        uint64_t connect_timeout_ms, io_timeout_ms, execution_timeout_ms, close_timeout_ms;
        uint64_t max_response_bytes;
    } cd_connection_options_t;

    typedef struct
    {
        uint32_t struct_size;
        cd_encoding_t encoding;
        uint64_t timeout_ms;
        cd_string_view_t new_line;
        uint64_t max_response_bytes;
        cd_receive_until_t receive_until;
        cd_stream_t read_stream, check_stream;
        cd_cancel_source_t* cancellation;
    } cd_call_options_t;

    typedef struct
    {
        uint32_t struct_size;
        int32_t overwrite;
        uint64_t timeout_ms;
        cd_cancel_source_t* cancellation;
    } cd_transfer_options_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t max_concurrency;
        cd_cancel_source_t* cancellation;
    } cd_batch_options_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_string_view_t host;
        uint16_t port;
        uint16_t reserved2;
        uint32_t reserved3;
    } cd_tcp_options_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_string_view_t remote_host, local_host;
        uint16_t remote_port, local_port;
        uint32_t reserved2;
    } cd_udp_options_t;

    typedef int32_t cd_parity_t;

    enum
    {
        CD_PARITY_NONE,
        CD_PARITY_ODD,
        CD_PARITY_EVEN
    };

    typedef int32_t cd_stop_bits_t;

    enum
    {
        CD_STOP_ONE,
        CD_STOP_ONE_POINT_FIVE,
        CD_STOP_TWO
    };

    typedef int32_t cd_flow_control_t;

    enum
    {
        CD_FLOW_NONE,
        CD_FLOW_SOFTWARE,
        CD_FLOW_HARDWARE
    };

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_string_view_t port_name;
        uint32_t baud_rate, data_bits;
        cd_parity_t parity;
        cd_stop_bits_t stop_bits;
        cd_flow_control_t flow_control;
        uint32_t reserved2;
    } cd_serial_options_t;

    typedef int32_t cd_auth_kind_t;

    enum
    {
        CD_AUTH_PASSWORD,
        CD_AUTH_PRIVATE_KEY
    };

    typedef int32_t cd_host_key_kind_t;

    enum
    {
        CD_HOST_KEY_KNOWN_HOSTS,
        CD_HOST_KEY_SHA256
    };

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_string_view_t host, user_name, password, private_key_path, passphrase, host_key;
        cd_string_view_t session_command;
        uint16_t port;
        uint16_t reserved2;
        cd_auth_kind_t auth;
        cd_host_key_kind_t host_key_kind;
        cd_shell_kind_t shell;
        cd_execution_mode_t execution_mode;
        uint32_t reserved3;
    } cd_ssh_options_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_string_view_t device_serial, executable_path, server_host, session_command;
        uint16_t server_port;
        uint16_t reserved2;
        cd_execution_mode_t execution_mode;
    } cd_adb_options_t;

    typedef struct
    {
        cd_string_view_t key, value;
    } cd_environment_entry_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_connection_options_t defaults;
        cd_shell_kind_t shell;
        cd_execution_mode_t execution_mode;
        cd_string_view_t executable_path, working_directory;
        const cd_string_view_t* arguments;
        uint64_t argument_count;
        const cd_environment_entry_t* environment;
        uint64_t environment_count;
    } cd_cli_options_t;

    typedef struct
    {
        uint32_t struct_size;
        cd_status_t code;
        cd_string_view_t message, backend;
        int64_t native_code;
        cd_string_view_t partial_text;
        cd_bytes_view_t partial_bytes;
        uint64_t confirmed_progress;
        int32_t has_confirmed_progress;
        uint32_t reserved;
        cd_string_view_t partial_file;
    } cd_error_info_t;

    typedef struct
    {
        uint32_t struct_size;
        int32_t passed;
        cd_string_view_t matched_text;
        cd_bytes_view_t matched_bytes;
        const cd_string_view_t* groups;
        uint64_t group_count;
        cd_string_view_t failure_reason;
        int32_t has_value;
        uint32_t reserved;
    } cd_check_result_t;

    typedef struct
    {
        uint32_t struct_size;
        cd_completion_reason_t completion_reason;
        int32_t exit_code, has_exit_code;
        uint64_t elapsed_ns;
    } cd_execution_info_t;

    typedef struct
    {
        uint32_t struct_size;
        uint32_t reserved;
        cd_string_view_t source, destination;
        uint64_t bytes_transferred, elapsed_ns;
        int32_t has_bytes_transferred;
        uint32_t reserved2;
    } cd_transfer_result_t;

    typedef int32_t cd_batch_item_kind_t;

    enum
    {
        CD_BATCH_EXECUTE_STRING,
        CD_BATCH_EXECUTE_BYTES,
        CD_BATCH_EXECUTE_UNTIL_STRING,
        CD_BATCH_EXECUTE_UNTIL_BYTES,
        CD_BATCH_UPLOAD,
        CD_BATCH_DOWNLOAD
    };

    typedef struct
    {
        uint32_t struct_size;
        cd_batch_item_kind_t kind;
        cd_string_view_t key;
        cd_connection_t* connection;
        cd_string_view_t command;
        cd_bytes_view_t bytes;
        const cd_check_t* check;
        const cd_call_options_t* options;
        cd_string_view_t source, destination;
        const cd_transfer_options_t* transfer_options;
    } cd_batch_item_t;

    typedef int32_t cd_batch_item_state_t;

    enum
    {
        CD_BATCH_COMPLETED,
        CD_BATCH_FAILED,
        CD_BATCH_TIMED_OUT,
        CD_BATCH_CANCELLED,
        CD_BATCH_NOT_STARTED
    };

    typedef struct
    {
        uint32_t struct_size;
        int32_t all_succeeded;
        int32_t was_cancelled;
        uint32_t reserved;
        uint64_t item_count, elapsed_ns;
    } cd_batch_info_t;

    typedef struct
    {
        uint32_t struct_size;
        cd_batch_item_state_t state;
        cd_string_view_t key;
        int32_t succeeded;
        uint32_t reserved;
        cd_result_t* result;
        uint64_t elapsed_ns;
    } cd_batch_item_result_t;

    typedef void(CD_CALL* cd_completion_fn)(cd_operation_t*, cd_result_t*, void* user_data);

    CD_API const char* CD_CALL cd_status_name(cd_status_t status);
    CD_API uint32_t CD_CALL cd_abi_version(void);
    CD_API void CD_CALL cd_connection_options_init(cd_connection_options_t* options);
#define CD_DECLARE_INIT(name)                                                                      \
    CD_API void CD_CALL cd_##name##_options_init(cd_##name##_options_t* options)
    CD_DECLARE_INIT(tcp);
    CD_DECLARE_INIT(udp);
    CD_DECLARE_INIT(serial);
    CD_DECLARE_INIT(ssh);
    CD_DECLARE_INIT(adb);
    CD_DECLARE_INIT(cli);
    CD_DECLARE_INIT(call);
    CD_DECLARE_INIT(transfer);
    CD_DECLARE_INIT(batch);
#undef CD_DECLARE_INIT
    CD_API cd_check_t CD_CALL cd_check_contains_string(cd_string_view_t value);
    CD_API cd_check_t CD_CALL cd_check_contains_bytes(cd_bytes_view_t value);
    CD_API cd_check_t CD_CALL cd_check_equals_string(cd_string_view_t value);
    CD_API cd_check_t CD_CALL cd_check_equals_bytes(cd_bytes_view_t value);
    CD_API cd_check_t CD_CALL cd_check_regex(cd_string_view_t pattern);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_bytes(uint64_t count);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_characters(uint64_t count);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_delimiter_string(cd_string_view_t delimiter,
                                                                        int32_t include);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_delimiter_bytes(cd_bytes_view_t delimiter,
                                                                       int32_t include);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_line(cd_string_view_t new_line);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_pattern(cd_string_view_t regex);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_idle(uint64_t interval_ms);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_message(void);
    CD_API cd_receive_until_t CD_CALL cd_receive_until_available(void);

    CD_API cd_status_t CD_CALL cd_driver_create(cd_driver_t** driver);
#define CD_DECLARE_CREATE(name)                                                                    \
    CD_API cd_status_t CD_CALL cd_create_##name(cd_driver_t*, const cd_##name##_options_t*,        \
                                                cd_connection_t**, cd_result_t** error)
    CD_DECLARE_CREATE(tcp);
    CD_DECLARE_CREATE(udp);
    CD_DECLARE_CREATE(serial);
    CD_DECLARE_CREATE(ssh);
    CD_DECLARE_CREATE(adb);
    CD_DECLARE_CREATE(cli);
#undef CD_DECLARE_CREATE
    CD_API cd_state_t CD_CALL cd_connection_get_state(cd_connection_t*);
    CD_API cd_capabilities_t CD_CALL cd_connection_get_capabilities(cd_connection_t*);
    CD_API cd_status_t CD_CALL cd_driver_close(cd_driver_t*, cd_result_t**);
    CD_API cd_status_t CD_CALL cd_driver_close_async(cd_driver_t*, cd_completion_fn, void*,
                                                     cd_operation_t**);

#define CD_DECLARE_SIMPLE(name)                                                                    \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, const cd_call_options_t*,               \
                                         cd_result_t**);                                           \
    CD_API cd_status_t CD_CALL cd_##name##_async(cd_connection_t*, const cd_call_options_t*,       \
                                                 cd_completion_fn, void*, cd_operation_t**)
    CD_DECLARE_SIMPLE(open);
    CD_DECLARE_SIMPLE(close);
    CD_DECLARE_SIMPLE(close_input);
    CD_DECLARE_SIMPLE(read_bytes);
    CD_DECLARE_SIMPLE(read_string);
    CD_DECLARE_SIMPLE(read_line);
#undef CD_DECLARE_SIMPLE
#define CD_DECLARE_EXACT(name)                                                                     \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, uint64_t count,                         \
                                         const cd_call_options_t*, cd_result_t**);                 \
    CD_API cd_status_t CD_CALL cd_##name##_async(cd_connection_t*, uint64_t count,                 \
                                                 const cd_call_options_t*, cd_completion_fn,       \
                                                 void*, cd_operation_t**)
    CD_DECLARE_EXACT(read_exactly_bytes);
    CD_DECLARE_EXACT(read_exactly_string);
#undef CD_DECLARE_EXACT
#define CD_DECLARE_UNTIL(name)                                                                     \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, const cd_receive_until_t*,              \
                                         const cd_call_options_t*, cd_result_t**);                 \
    CD_API cd_status_t CD_CALL cd_##name##_async(cd_connection_t*, const cd_receive_until_t*,      \
                                                 const cd_call_options_t*, cd_completion_fn,       \
                                                 void*, cd_operation_t**)
    CD_DECLARE_UNTIL(read_until_bytes);
    CD_DECLARE_UNTIL(read_until_string);
#undef CD_DECLARE_UNTIL
#define CD_DECLARE_WRITE(name, type)                                                               \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, type, const cd_call_options_t*,         \
                                         cd_result_t**);                                           \
    CD_API cd_status_t CD_CALL cd_##name##_async(cd_connection_t*, type, const cd_call_options_t*, \
                                                 cd_completion_fn, void*, cd_operation_t**)
    CD_DECLARE_WRITE(write_bytes, cd_bytes_view_t);
    CD_DECLARE_WRITE(write_string, cd_string_view_t);
    CD_DECLARE_WRITE(write_line, cd_string_view_t);
#undef CD_DECLARE_WRITE
#define CD_DECLARE_EXEC(name, type)                                                                \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, type, const cd_check_t*,                \
                                         const cd_call_options_t*, cd_result_t**);                 \
    CD_API cd_status_t CD_CALL cd_##name##_async(cd_connection_t*, type, const cd_check_t*,        \
                                                 const cd_call_options_t*, cd_completion_fn,       \
                                                 void*, cd_operation_t**)
    CD_DECLARE_EXEC(execute_string, cd_string_view_t);
    CD_DECLARE_EXEC(execute_bytes, cd_bytes_view_t);
    CD_DECLARE_EXEC(execute_until_string, cd_string_view_t);
    CD_DECLARE_EXEC(execute_until_bytes, cd_bytes_view_t);
#undef CD_DECLARE_EXEC
#define CD_DECLARE_TRANSFER(name)                                                                  \
    CD_API cd_status_t CD_CALL cd_##name(cd_connection_t*, cd_string_view_t source,                \
                                         cd_string_view_t destination,                             \
                                         const cd_transfer_options_t*, cd_result_t**);             \
    CD_API cd_status_t CD_CALL cd_##name##_async(                                                  \
        cd_connection_t*, cd_string_view_t source, cd_string_view_t destination,                   \
        const cd_transfer_options_t*, cd_completion_fn, void*, cd_operation_t**)
    CD_DECLARE_TRANSFER(upload);
    CD_DECLARE_TRANSFER(download);
#undef CD_DECLARE_TRANSFER
    CD_API cd_status_t CD_CALL cd_batch_run(cd_driver_t*, const cd_batch_item_t*, uint64_t count,
                                            const cd_batch_options_t*, cd_result_t**);
    CD_API cd_status_t CD_CALL cd_batch_run_async(cd_driver_t*, const cd_batch_item_t*,
                                                  uint64_t count, const cd_batch_options_t*,
                                                  cd_completion_fn, void*, cd_operation_t**);
    CD_API cd_status_t CD_CALL cd_response_checker_is_match_string(cd_string_view_t,
                                                                   const cd_check_t*,
                                                                   cd_result_t**);
    CD_API cd_status_t CD_CALL cd_response_checker_is_match_bytes(cd_bytes_view_t,
                                                                  const cd_check_t*, cd_result_t**);

    CD_API int32_t CD_CALL cd_operation_is_completed(cd_operation_t*);
    /* On success, wait returns a retained result; observer timeout does not cancel. */
    CD_API cd_status_t CD_CALL cd_operation_wait(cd_operation_t*, uint64_t timeout_ms,
                                                 cd_result_t**);
    CD_API void CD_CALL cd_operation_cancel(cd_operation_t*);
    CD_API cd_status_t CD_CALL cd_cancel_source_create(cd_cancel_source_t**);
    CD_API void CD_CALL cd_cancel_source_request(cd_cancel_source_t*);
    CD_API int32_t CD_CALL cd_cancel_source_is_requested(cd_cancel_source_t*);
#define CD_DECLARE_LIFETIME(name)                                                                  \
    CD_API void CD_CALL cd_##name##_retain(cd_##name##_t*);                                        \
    CD_API void CD_CALL cd_##name##_release(cd_##name##_t*)
    CD_DECLARE_LIFETIME(driver);
    CD_DECLARE_LIFETIME(connection);
    CD_DECLARE_LIFETIME(operation);
    CD_DECLARE_LIFETIME(result);
    CD_DECLARE_LIFETIME(cancel_source);
#undef CD_DECLARE_LIFETIME
    CD_API cd_status_t CD_CALL cd_result_get_status(const cd_result_t*);
    CD_API cd_result_kind_t CD_CALL cd_result_get_kind(const cd_result_t*);
    CD_API cd_status_t CD_CALL cd_result_get_error(const cd_result_t*, cd_error_info_t*);
    CD_API cd_status_t CD_CALL cd_result_get_text(const cd_result_t*, cd_stream_t,
                                                  cd_string_view_t*);
    CD_API cd_status_t CD_CALL cd_result_get_bytes(const cd_result_t*, cd_stream_t,
                                                   cd_bytes_view_t*);
    CD_API cd_status_t CD_CALL cd_result_get_check(const cd_result_t*, cd_check_result_t*);
    CD_API cd_status_t CD_CALL cd_result_get_execution_info(const cd_result_t*,
                                                            cd_execution_info_t*);
    CD_API cd_status_t CD_CALL cd_result_get_transfer(const cd_result_t*, cd_transfer_result_t*);
    CD_API cd_status_t CD_CALL cd_result_get_batch_info(const cd_result_t*, cd_batch_info_t*);
    /* item.result is borrowed, retain it to outlive the batch result. */
    CD_API cd_status_t CD_CALL cd_result_get_batch_item(const cd_result_t*, uint64_t index,
                                                        cd_batch_item_result_t*);
#ifdef __cplusplus
}
#endif
#endif
