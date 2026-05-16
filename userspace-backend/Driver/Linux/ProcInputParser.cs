using System;
using System.Collections.Generic;

namespace userspace_backend.Driver.Linux
{
    // Parses /proc/bus/input/devices. Each record is separated by a blank
    // line; lines start with a single letter and a colon. Only fields we
    // need are kept (I/N/H); the rest are ignored. See linux/Documentation/
    // input/input.rst for the format.
    internal static class ProcInputParser
    {
        public readonly struct Record
        {
            public Record(string name, ushort vendor, ushort product, bool isMouse)
            {
                Name = name;
                Vendor = vendor;
                Product = product;
                IsMouse = isMouse;
            }

            public string Name { get; }
            public ushort Vendor { get; }
            public ushort Product { get; }
            public bool IsMouse { get; }

            public string Hwid => string.Format(
                @"HID\VID_{0:X4}&PID_{1:X4}", Vendor, Product);
        }

        public static IEnumerable<Record> Parse(string content)
        {
            string name = string.Empty;
            ushort vendor = 0;
            ushort product = 0;
            bool isMouse = false;
            bool sawAny = false;

            foreach (var rawLine in content.Split('\n'))
            {
                var line = rawLine.TrimEnd('\r');
                if (line.Length == 0)
                {
                    if (sawAny)
                    {
                        yield return new Record(name, vendor, product, isMouse);
                    }
                    name = string.Empty;
                    vendor = 0;
                    product = 0;
                    isMouse = false;
                    sawAny = false;
                    continue;
                }

                if (line.Length < 3 || line[1] != ':') continue;
                sawAny = true;
                var prefix = line[0];
                var rest = line.Substring(2).TrimStart();

                switch (prefix)
                {
                    case 'I':
                        ParseIdentLine(rest, out vendor, out product);
                        break;
                    case 'N':
                        name = ParseQuotedField(rest, "Name=");
                        break;
                    case 'H':
                        isMouse = HandlersIncludeMouse(rest);
                        break;
                }
            }

            if (sawAny)
            {
                yield return new Record(name, vendor, product, isMouse);
            }
        }

        private static void ParseIdentLine(string rest, out ushort vendor, out ushort product)
        {
            vendor = 0;
            product = 0;
            foreach (var token in rest.Split(' '))
            {
                var eq = token.IndexOf('=');
                if (eq < 0) continue;
                var key = token.Substring(0, eq);
                var val = token.Substring(eq + 1);
                if (key == "Vendor") ushort.TryParse(
                    val, System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture, out vendor);
                else if (key == "Product") ushort.TryParse(
                    val, System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.CultureInfo.InvariantCulture, out product);
            }
        }

        private static string ParseQuotedField(string rest, string key)
        {
            if (!rest.StartsWith(key, StringComparison.Ordinal)) return string.Empty;
            var after = rest.Substring(key.Length);
            if (after.Length < 2 || after[0] != '"') return after;
            var end = after.LastIndexOf('"');
            if (end <= 0) return string.Empty;
            return after.Substring(1, end - 1);
        }

        private static bool HandlersIncludeMouse(string rest)
        {
            // H: Handlers=event6 mouse0
            var eq = rest.IndexOf('=');
            if (eq < 0) return false;
            foreach (var tok in rest.Substring(eq + 1).Split(' '))
            {
                if (tok.Length < 5) continue;
                if (!tok.StartsWith("mouse", StringComparison.Ordinal)) continue;
                if (IsAllDigits(tok, 5)) return true;
            }
            return false;
        }

        private static bool IsAllDigits(string s, int from)
        {
            if (from >= s.Length) return false;
            for (int i = from; i < s.Length; i++)
            {
                if (s[i] < '0' || s[i] > '9') return false;
            }
            return true;
        }
    }
}
