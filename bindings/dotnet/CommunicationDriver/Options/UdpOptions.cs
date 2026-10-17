using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class UdpOptions : ConnectionOptions
    {
        public string RemoteHost { get; set; }

        public ushort RemotePort { get; set; }

        public string LocalHost { get; set; }

        public ushort LocalPort { get; set; }
    }
}
