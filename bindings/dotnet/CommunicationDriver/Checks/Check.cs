using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class Check
    {
        internal int Kind;
        internal string Text;
        internal byte[] Bytes;

        private Check(int kind, string text, byte[] bytes)
        {
            Kind = kind;
            Text = text;
            Bytes = bytes == null ? null : (byte[])bytes.Clone();
        }

        public static Check Contains(string value)
        {
            return new Check(1, value ?? throw new ArgumentNullException(nameof(value)), null);
        }

        public static Check Contains(byte[] value)
        {
            return new Check(1, null, value ?? throw new ArgumentNullException(nameof(value)));
        }

        public static Check Equals(string value)
        {
            return new Check(2, value ?? throw new ArgumentNullException(nameof(value)), null);
        }

        public static Check Equals(byte[] value)
        {
            return new Check(2, null, value ?? throw new ArgumentNullException(nameof(value)));
        }

        public static Check Regex(string pattern)
        {
            return new Check(3, pattern ?? throw new ArgumentNullException(nameof(pattern)), null);
        }
    }
}
