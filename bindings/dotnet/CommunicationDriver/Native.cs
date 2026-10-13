using System;
using System.Runtime.InteropServices;
namespace CommunicationDriver {
[StructLayout(LayoutKind.Sequential)] internal struct NStringView {
 internal IntPtr data;
 internal ulong size;
}
[StructLayout(LayoutKind.Sequential)] internal struct NBytesView {
 internal IntPtr data;
 internal ulong size;
}
[StructLayout(LayoutKind.Sequential)] internal struct NCheck {
 internal uint struct_size;
 internal int kind;
 internal int is_text;
 internal int reserved;
 internal NStringView text;
 internal NBytesView bytes;
}
[StructLayout(LayoutKind.Sequential)] internal struct NReceiveUntil {
 internal uint struct_size;
 internal int kind;
 internal ulong count;
 internal NStringView text;
 internal NBytesView bytes;
 internal int is_text;
 internal int include_delimiter;
 internal ulong idle_ms;
}
[StructLayout(LayoutKind.Sequential)] internal struct NConnectionOptions {
 internal uint struct_size;
 internal int encoding;
 internal NStringView new_line;
 internal NReceiveUntil receive_until;
 internal ulong connect_timeout_ms, io_timeout_ms, execution_timeout_ms, close_timeout_ms;
 internal ulong max_response_bytes;
}
[StructLayout(LayoutKind.Sequential)] internal struct NCallOptions {
 internal uint struct_size;
 internal int encoding;
 internal ulong timeout_ms;
 internal NStringView new_line;
 internal ulong max_response_bytes;
 internal NReceiveUntil receive_until;
 internal int read_stream, check_stream;
 internal IntPtr cancellation;
}
[StructLayout(LayoutKind.Sequential)] internal struct NTransferOptions {
 internal uint struct_size;
 internal int overwrite;
 internal ulong timeout_ms;
 internal IntPtr cancellation;
}
[StructLayout(LayoutKind.Sequential)] internal struct NBatchOptions {
 internal uint struct_size;
 internal uint max_concurrency;
 internal IntPtr cancellation;
}
[StructLayout(LayoutKind.Sequential)] internal struct NTcpOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal NStringView host;
 internal ushort port;
 internal ushort reserved2;
 internal uint reserved3;
}
[StructLayout(LayoutKind.Sequential)] internal struct NUdpOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal NStringView remote_host, local_host;
 internal ushort remote_port, local_port;
 internal uint reserved2;
}
[StructLayout(LayoutKind.Sequential)] internal struct NSerialOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal NStringView port_name;
 internal uint baud_rate, data_bits;
 internal int parity;
 internal int stop_bits;
 internal int flow_control;
 internal uint reserved2;
}
[StructLayout(LayoutKind.Sequential)] internal struct NSshOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal NStringView host, user_name, password, private_key_path, passphrase, host_key;
 internal NStringView session_command;
 internal ushort port;
 internal ushort reserved2;
 internal int auth;
 internal int host_key_kind;
 internal int shell;
 internal int execution_mode;
 internal uint reserved3;
}
[StructLayout(LayoutKind.Sequential)] internal struct NAdbOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal NStringView device_serial, executable_path, server_host, session_command;
 internal ushort server_port;
 internal ushort reserved2;
 internal int execution_mode;
}
[StructLayout(LayoutKind.Sequential)] internal struct NEnvironmentEntry {
 internal NStringView key, value;
}
[StructLayout(LayoutKind.Sequential)] internal struct NCliOptions {
 internal uint struct_size;
 internal uint reserved;
 internal NConnectionOptions defaults;
 internal int shell;
 internal int execution_mode;
 internal NStringView executable_path, working_directory;
 internal IntPtr arguments;
 internal ulong argument_count;
 internal IntPtr environment;
 internal ulong environment_count;
}
[StructLayout(LayoutKind.Sequential)] internal struct NErrorInfo {
 internal uint struct_size;
 internal int code;
 internal NStringView message, backend;
 internal long native_code;
 internal NStringView partial_text;
 internal NBytesView partial_bytes;
 internal ulong confirmed_progress;
 internal int has_confirmed_progress;
 internal uint reserved;
 internal NStringView partial_file;
}
[StructLayout(LayoutKind.Sequential)] internal struct NCheckResult {
 internal uint struct_size;
 internal int passed;
 internal NStringView matched_text;
 internal NBytesView matched_bytes;
 internal IntPtr groups;
 internal ulong group_count;
 internal NStringView failure_reason;
 internal int has_value;
 internal uint reserved;
}
[StructLayout(LayoutKind.Sequential)] internal struct NExecutionInfo {
 internal uint struct_size;
 internal int completion_reason;
 internal int exit_code, has_exit_code;
 internal ulong elapsed_ns;
}
[StructLayout(LayoutKind.Sequential)] internal struct NTransferResult {
 internal uint struct_size;
 internal uint reserved;
 internal NStringView source, destination;
 internal ulong bytes_transferred, elapsed_ns;
 internal int has_bytes_transferred;
 internal uint reserved2;
}
[StructLayout(LayoutKind.Sequential)] internal struct NBatchItem {
 internal uint struct_size;
 internal int kind;
 internal NStringView key;
 internal IntPtr connection;
 internal NStringView command;
 internal NBytesView bytes;
 internal IntPtr check;
 internal IntPtr options;
 internal NStringView source, destination;
 internal IntPtr transfer_options;
}
[StructLayout(LayoutKind.Sequential)] internal struct NBatchInfo {
 internal uint struct_size;
 internal int all_succeeded;
 internal int was_cancelled;
 internal uint reserved;
 internal ulong item_count, elapsed_ns;
}
[StructLayout(LayoutKind.Sequential)] internal struct NBatchItemResult {
 internal uint struct_size;
 internal int state;
 internal NStringView key;
 internal int succeeded;
 internal uint reserved;
 internal IntPtr result;
 internal ulong elapsed_ns;
}
internal static class Native {
 internal const string Library="communicationdriver_native";
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void Completion(IntPtr operation,IntPtr result,IntPtr user);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern uint cd_abi_version();
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr cd_status_name(int status);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_driver_create(out IntPtr driver);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_driver_retain(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_driver_release(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_connection_retain(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_connection_release(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_operation_retain(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_operation_release(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_result_retain(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_result_release(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_cancel_source_retain(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_cancel_source_release(IntPtr handle);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_tcp_options_init(out NTcpOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_tcp(IntPtr driver, ref NTcpOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_udp_options_init(out NUdpOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_udp(IntPtr driver, ref NUdpOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_serial_options_init(out NSerialOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_serial(IntPtr driver, ref NSerialOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_ssh_options_init(out NSshOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_ssh(IntPtr driver, ref NSshOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_adb_options_init(out NAdbOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_adb(IntPtr driver, ref NAdbOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_cli_options_init(out NCliOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_create_cli(IntPtr driver, ref NCliOptions options, out IntPtr connection, out IntPtr error);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_call_options_init(out NCallOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_transfer_options_init(out NTransferOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_batch_options_init(out NBatchOptions options);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_connection_get_state(IntPtr connection);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern ulong cd_connection_get_capabilities(IntPtr connection);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_driver_close_async(IntPtr driver, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_open_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_close_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_close_input_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_bytes_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_string_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_line_async(IntPtr connection, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_exactly_bytes_async(IntPtr connection, ulong count, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_exactly_string_async(IntPtr connection, ulong count, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_until_bytes_async(IntPtr connection, IntPtr rule, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_read_until_string_async(IntPtr connection, IntPtr rule, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_write_bytes_async(IntPtr connection, NBytesView input, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_write_string_async(IntPtr connection, NStringView input, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_write_line_async(IntPtr connection, NStringView input, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_execute_string_async(IntPtr connection, NStringView input, IntPtr check, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_execute_bytes_async(IntPtr connection, NBytesView input, IntPtr check, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_execute_until_string_async(IntPtr connection, NStringView input, IntPtr check, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_execute_until_bytes_async(IntPtr connection, NBytesView input, IntPtr check, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_upload_async(IntPtr connection, NStringView source, NStringView destination, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_download_async(IntPtr connection, NStringView source, NStringView destination, IntPtr options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_batch_run_async(IntPtr driver, IntPtr items, ulong count, ref NBatchOptions options, Completion callback, IntPtr user, out IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_response_checker_is_match_string(NStringView response, ref NCheck check, out IntPtr result);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_response_checker_is_match_bytes(NBytesView response, ref NCheck check, out IntPtr result);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_operation_cancel(IntPtr operation);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_cancel_source_create(out IntPtr source);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern void cd_cancel_source_request(IntPtr source);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_status(IntPtr result);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_kind(IntPtr result);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_error(IntPtr result, ref NErrorInfo value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_check(IntPtr result, ref NCheckResult value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_execution_info(IntPtr result, ref NExecutionInfo value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_transfer(IntPtr result, ref NTransferResult value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_batch_info(IntPtr result, ref NBatchInfo value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_text(IntPtr result, int stream, out NStringView value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_bytes(IntPtr result, int stream, out NBytesView value);
 [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] internal static extern int cd_result_get_batch_item(IntPtr result, ulong index, ref NBatchItemResult value);
}
}
