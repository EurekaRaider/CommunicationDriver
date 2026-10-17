using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace CommunicationDriver
{
    public sealed class Driver : IDisposable
    {
        internal readonly NativeHandle Handle;

        public Driver()
        {
            IntPtr h;
            Interop.Checked(Native.cd_driver_create(out h));
            Handle = new NativeHandle(h, 0);

            if (Native.cd_abi_version() != 1)
            {
                Handle.Dispose();
                throw new InvalidOperationException("Unsupported native ABI version");
            }
        }

        private Connection Created(int status, IntPtr h, IntPtr e)
        {
            using (var error = new NativeHandle(e, 3))
            {
                Interop.Checked(status, e);
                return new Connection(this, new NativeHandle(h, 1));
            }
        }

        public Connection CreateTcp(TcpOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NTcpOptions o;
                Native.cd_tcp_options_init(out o);
                o.defaults = a.Defaults(options);
                o.host = a.Text(options.Host);
                o.port = options.Port;

                IntPtr c,
                    e;
                return Created(Native.cd_create_tcp(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Connection CreateUdp(UdpOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NUdpOptions o;
                Native.cd_udp_options_init(out o);
                o.defaults = a.Defaults(options);
                o.remote_host = a.Text(options.RemoteHost);
                o.remote_port = options.RemotePort;
                o.local_host = a.Text(options.LocalHost);
                o.local_port = options.LocalPort;

                IntPtr c,
                    e;
                return Created(Native.cd_create_udp(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Connection CreateSerial(SerialOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NSerialOptions o;
                Native.cd_serial_options_init(out o);
                o.defaults = a.Defaults(options);
                o.port_name = a.Text(options.PortName);
                o.baud_rate = options.BaudRate;
                o.data_bits = options.DataBits;
                o.parity = (int)options.Parity;
                o.stop_bits = (int)options.StopBits;
                o.flow_control = (int)options.FlowControl;

                IntPtr c,
                    e;
                return Created(Native.cd_create_serial(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Connection CreateSsh(SshOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));
            if (options.Auth == null || options.HostKey == null)
                throw new ArgumentException("Auth and HostKey are required", nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NSshOptions o;
                Native.cd_ssh_options_init(out o);
                o.defaults = a.Defaults(options);
                o.host = a.Text(options.Host);
                o.port = options.Port;
                o.user_name = a.Text(options.UserName);
                o.auth = options.Auth.Kind;

                if (o.auth == 0)
                    o.password = a.Text(options.Auth.Value);
                else
                    o.private_key_path = a.Text(options.Auth.Value);

                o.passphrase = a.Text(options.Auth.Passphrase);
                o.host_key_kind = options.HostKey.Kind;
                o.host_key = a.Text(options.HostKey.Value);
                o.shell = (int)options.Shell;
                o.execution_mode = (int)options.ExecutionMode;
                o.session_command = a.Text(options.SessionCommand);

                IntPtr c,
                    e;
                return Created(Native.cd_create_ssh(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Connection CreateAdb(AdbOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NAdbOptions o;
                Native.cd_adb_options_init(out o);
                o.defaults = a.Defaults(options);
                o.device_serial = a.Text(options.DeviceSerial);
                o.executable_path = a.Text(options.ExecutablePath);
                o.server_host = a.Text(options.ServerHost);
                o.server_port = options.ServerPort;
                o.execution_mode = (int)options.ExecutionMode;
                o.session_command = a.Text(options.SessionCommand);

                IntPtr c,
                    e;
                return Created(Native.cd_create_adb(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Connection CreateCli(CliOptions options)
        {
            if (options == null)
                throw new ArgumentNullException(nameof(options));

            using (var l = new HandleLease(Handle))
            using (var a = new Arena())
            {
                NCliOptions o;
                Native.cd_cli_options_init(out o);
                o.defaults = a.Defaults(options);
                o.shell = (int)options.Shell;
                o.execution_mode = (int)options.ExecutionMode;
                o.executable_path = a.Text(options.ExecutablePath);
                o.working_directory = a.Text(options.WorkingDirectory);

                var args = new List<NStringView>();

                foreach (var arg in options.Arguments)
                    args.Add(a.Text(arg));

                o.arguments = a.Array(args);
                o.argument_count = (ulong)args.Count;

                var env = new List<NEnvironmentEntry>();

                foreach (var pair in options.Environment)
                    env.Add(
                        new NEnvironmentEntry { key = a.Text(pair.Key), value = a.Text(pair.Value) }
                    );

                o.environment = a.Array(env);
                o.environment_count = (ulong)env.Count;

                IntPtr c,
                    e;
                return Created(Native.cd_create_cli(l.Pointer, ref o, out c, out e), c, e);
            }
        }

        public Task CloseAsync()
        {
            return Interop.Start(
                new[] { Handle },
                CancellationToken.None,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) => Native.cd_driver_close_async(h, cb, user, out op),
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void Close()
        {
            CloseAsync().GetAwaiter().GetResult();
        }

        public void Dispose()
        {
            if (Handle.IsClosed)
                return;
            try
            {
                Close();
            }
            catch (ObjectDisposedException) when (Handle.IsClosed) { }
            finally
            {
                Handle.Dispose();
            }
        }
    }
}
