using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class AdbOptions : ConnectionOptions
    {
        public string DeviceSerial { get; set; }

        public string ExecutablePath { get; set; }

        public string ServerHost { get; set; } = "127.0.0.1";

        public ushort ServerPort { get; set; } = 5037;

        public ExecutionMode ExecutionMode { get; set; }

        public string SessionCommand { get; set; }
    }
}
