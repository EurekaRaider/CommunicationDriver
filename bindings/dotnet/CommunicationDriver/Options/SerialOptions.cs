using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class SerialOptions : ConnectionOptions
    {
        public string PortName { get; set; }

        public uint BaudRate { get; set; } = 115200;

        public uint DataBits { get; set; } = 8;

        public Parity Parity { get; set; }

        public StopBits StopBits { get; set; }

        public FlowControl FlowControl { get; set; }
    }
}
