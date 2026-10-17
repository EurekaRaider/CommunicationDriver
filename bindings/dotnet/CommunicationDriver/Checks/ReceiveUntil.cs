using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class ReceiveUntil
    {
        internal int Kind;
        internal ulong Count,
            IdleMs;
        internal string Text;
        internal byte[] Data;
        internal bool Include;

        private ReceiveUntil(int kind)
        {
            Kind = kind;
        }

        public static ReceiveUntil Bytes(ulong count)
        {
            if (count == 0)
                throw new ArgumentOutOfRangeException(nameof(count));
            return new ReceiveUntil(1) { Count = count };
        }

        public static ReceiveUntil Characters(ulong count)
        {
            if (count == 0)
                throw new ArgumentOutOfRangeException(nameof(count));
            return new ReceiveUntil(2) { Count = count };
        }

        public static ReceiveUntil Delimiter(string delimiter, bool includeDelimiter = false)
        {
            if (string.IsNullOrEmpty(delimiter))
                throw new ArgumentException("Delimiter required", nameof(delimiter));
            return new ReceiveUntil(3) { Text = delimiter, Include = includeDelimiter };
        }

        public static ReceiveUntil Delimiter(byte[] delimiter, bool includeDelimiter = false)
        {
            if (delimiter == null || delimiter.Length == 0)
                throw new ArgumentException("Delimiter required", nameof(delimiter));
            return new ReceiveUntil(3)
            {
                Data = (byte[])delimiter.Clone(),
                Include = includeDelimiter,
            };
        }

        public static ReceiveUntil Line(string newLine = "\n")
        {
            return Delimiter(newLine);
        }

        public static ReceiveUntil Pattern(string regex)
        {
            return new ReceiveUntil(4)
            {
                Text = regex ?? throw new ArgumentNullException(nameof(regex)),
            };
        }

        public static ReceiveUntil Idle(TimeSpan interval)
        {
            return new ReceiveUntil(5) { IdleMs = Interop.Milliseconds(interval) };
        }

        public static ReceiveUntil Message()
        {
            return new ReceiveUntil(6);
        }

        public static ReceiveUntil Available()
        {
            return new ReceiveUntil(7);
        }
    }
}
