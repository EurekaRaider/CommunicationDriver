using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class SshOptions : ConnectionOptions
    {
        public string Host { get; set; }

        public ushort Port { get; set; } = 22;

        public string UserName { get; set; }

        public SshAuth Auth { get; set; }

        public HostKey HostKey { get; set; }

        public ShellKind Shell { get; set; }

        public ExecutionMode ExecutionMode { get; set; }

        public string SessionCommand { get; set; }
    }
}
