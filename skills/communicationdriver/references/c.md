# C99 ABI consumers

Include `<communicationdriver/communicationdriver.h>` and link the native
library. Public declarations are C99 compatible. Do not redeclare ABI structs
or use the C++ wrapper's layout for an FFI.

## Runnable CLI example with cleanup

Release every returned result, including error and close results. Check getter
statuses. Text/byte views are length-delimited, not guaranteed NUL-terminated.

```c
#include <communicationdriver/communicationdriver.h>
#include <stdio.h>

int main(void) {
 cd_driver_t* driver = NULL;
 cd_connection_t* connection = NULL;
 cd_result_t* result = NULL;
 cd_cli_options_t options;
 cd_string_view_t response = {0};
 cd_check_result_t checked = {0};
 cd_execution_info_t execution = {0};
 cd_status_t status;
 int code = 1;
 cd_cli_options_init(&options);
 options.execution_mode = CD_ONE_SHOT;
 checked.struct_size = sizeof(checked);
 execution.struct_size = sizeof(execution);
 status = cd_driver_create(&driver);
 if(status != CD_OK) goto cleanup;
 status = cd_create_cli(driver, &options, &connection, &result);
 if(status != CD_OK) goto cleanup;
 cd_result_release(result); result = NULL;
 status = cd_open(connection, NULL, &result);
 if(status != CD_OK) goto cleanup;
 cd_result_release(result); result = NULL;
 {
  cd_check_t check = cd_check_contains_string(CD_SV("READY"));
  status = cd_execute_string(connection, CD_SV("echo READY"), &check, NULL, &result);
 }
 if(status != CD_OK) goto cleanup;
 status = cd_result_get_text(result, CD_STREAM_STDOUT, &response);
 if(status != CD_OK) goto cleanup;
 status = cd_result_get_check(result, &checked);
 if(status != CD_OK) goto cleanup;
 status = cd_result_get_execution_info(result, &execution);
 if(status != CD_OK) goto cleanup;
 fwrite(response.data, 1, (size_t)response.size, stdout);
 code = checked.has_value && checked.passed && execution.has_exit_code && execution.exit_code == 0 ? 0 : 1;
cleanup:
 if(status != CD_OK) {
  cd_error_info_t error = {0};
  error.struct_size = sizeof(error);
  fprintf(stderr, "%s\n", cd_status_name(status));
  if(result && cd_result_get_error(result, &error) == CD_OK && error.message.size) {
   fwrite(error.message.data, 1, (size_t)error.message.size, stderr);
   fputc('\n', stderr);
  }
 }
 cd_result_release(result); result = NULL;
 if(connection) {
  cd_status_t closed = cd_close(connection, NULL, &result);
  if(closed != CD_OK) code = 1;
  cd_result_release(result);
 }
 cd_connection_release(connection);
 cd_driver_release(driver);
 return code;
}
```

## Factories and framing

Use cd_driver_create then cd_create_tcp/udp/serial/ssh/adb/cli, each taking
`(driver, &options, &connection, &error_result)`. First call the corresponding
cd_*_options_init, including call/transfer/batch options. Common options are
nested in options.defaults. Configure raw TCP as:

```c
cd_status_t create_framed_tcp(cd_driver_t* driver, cd_string_view_t host,
 uint16_t port, cd_connection_t** connection, cd_result_t** error) {
 cd_tcp_options_t options;
 cd_tcp_options_init(&options);
 options.host = host;
 options.port = port;
 options.defaults.receive_until = cd_receive_until_delimiter_string(CD_SV("END"), 0);
 return cd_create_tcp(driver, &options, connection, error);
}
```

This assumes END is the real response boundary; create still needs cd_open.
Other rules: cd_receive_until_bytes/characters/delimiter_string/delimiter_bytes/
line/pattern/message. message is UDP-only.
`cd_execute_string(connection, command, check_or_NULL, call_options_or_NULL, &result)`
sends exactly command; include the protocol newline yourself. Use cd_execute_bytes
and cd_bytes_view_t for binary data. CD_SV is for string literals/arrays only;
for a pointer use `(cd_string_view_t){pointer, actual_byte_count}`.

SSH options require user_name, auth plus password/private_key_path, and
host_key_kind plus host_key (CD_HOST_KEY_KNOWN_HOSTS or CD_HOST_KEY_SHA256).
ADB requires device_serial; supply executable_path if adb is not on PATH.
Backend fields follow the public C header, not wrapper property names.

## Ownership, async, and cancellation

- Set output structures' struct_size before getters (error, check, execution,
  transfer, batch info/item). Read optional values only with their has_* flag.
  Ordinary mismatches return CD_OK with checked.passed == 0.
- Driver, connection, operation, result, cancel_source handles each have matching
  retain/release. Getters return borrowed views; copy them or retain the owning
  result before keeping them beyond that result's lifetime.
- A `_async` submit returns an operation handle. CD_OK means accepted, not I/O
  success. Exactly one non-inline completion callback follows acceptance;
  submission failure produces no callback.
- Callback: `void CD_CALL done(cd_operation_t*, cd_result_t*, void*)`. Its result
  is borrowed: retain to save it; do not release the borrowed reference. Keep
  user_data alive through completion. Do not synchronously wait/close in the
  operation's own completion context; use async calls.
- cd_operation_wait returns an owned result on successful observation. Inspect
  cd_result_get_status for the operation status, then release result. Observer
  timeout does not cancel. CD_INFINITE applies only to observer wait.
- cd_operation_cancel or cd_cancel_source_request requests cancellation; observe
  completion afterwards. A cancel source goes in call/transfer/batch options
  and must be released. Async inputs are copied before submit returns.
- Preserve cd_error_info_t partial_text/partial_bytes/partial_file while result
  is alive. CD_STATE_NEEDS_RECOVERY requires cd_close then cd_open, with no
  automatic command retry.

## Batch and transfers

Initialize cd_batch_options_t (default max_concurrency 32). Zero-initialize each
cd_batch_item_t and set struct_size, kind, key, connection plus its command/
bytes/check/options or source/destination/transfer_options. Connections must
already be open and belong to the Driver passed to cd_batch_run[_async]. Keys
must be unique. Cancel through batch options or the returned batch operation.
Read the full report with cd_result_get_batch_info and cd_result_get_batch_item;
item result handles are borrowed from the batch result. Retain to outlive it.

Check CD_CAP_UPLOAD/CD_CAP_DOWNLOAD for optional file transfers. cd_upload takes
local source then remote destination; cd_download takes remote source then
local destination. cd_transfer_options_init defaults overwrite to true.
cd_result_get_transfer reports known progress; failures can report partial_file.
Recursive directory transfer is not provided.
