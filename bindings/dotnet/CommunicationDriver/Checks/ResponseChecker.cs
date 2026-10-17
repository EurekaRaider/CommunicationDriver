using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace CommunicationDriver
{
    public static class ResponseChecker
    {
        public static bool IsMatch(string response, Check check)
        {
            if (response == null || check == null)
                throw new ArgumentNullException();

            using (var a = new Arena())
            {
                var c = a.Check(check);
                IntPtr r;
                var status = Native.cd_response_checker_is_match_string(
                    a.Text(response),
                    ref c,
                    out r
                );

                using (var h = new NativeHandle(r, 3))
                {
                    Interop.Checked(status, r);
                    return Interop.CheckResult(r).Passed;
                }
            }
        }

        public static bool IsMatch(byte[] response, Check check)
        {
            if (response == null || check == null)
                throw new ArgumentNullException();

            using (var a = new Arena())
            {
                var c = a.Check(check);
                IntPtr r;
                var status = Native.cd_response_checker_is_match_bytes(
                    a.Bytes(response),
                    ref c,
                    out r
                );

                using (var h = new NativeHandle(r, 3))
                {
                    Interop.Checked(status, r);
                    return Interop.CheckResult(r).Passed;
                }
            }
        }
    }
}
