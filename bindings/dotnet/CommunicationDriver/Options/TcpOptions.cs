using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class TcpOptions : ConnectionOptions
    {
        public string Host { get; set; }

        public ushort Port { get; set; }
    }
}
