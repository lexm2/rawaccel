using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace userspace_backend.Data.Profiles
{
    public class Anisotropy
    {
        public Vector2 Domain { get; set; } = new Vector2();

        public Vector2 Range { get; set; } = new Vector2();

        public double LPNorm { get; set; }

        public bool CombineXYComponents { get; set; }
    }
}
