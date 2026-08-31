// Project Seoul browser-vetted ad-block replacement resources.

#include "seoul/browser/adblock/ad_block_resource_catalog.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/sha2.h"

namespace seoul::adblock {
namespace {

struct BundledResource {
  const char* name;
  // Every name a filter rule may use to reach this resource.
  //
  // A rule that names a resource Seoul cannot resolve does not fall back to
  // doing nothing: the engine still reports the request as matched but with no
  // redirect, so the request is hard-blocked instead of answered with a stub.
  // A missing alias therefore turns a designed no-op into exactly the breakage
  // the stub exists to prevent, which is why the canonical underscore spellings
  // used by the upstream lists are registered beside the host/path form.
  //
  // Unused slots are empty; BuildCatalog skips anything unset.
  std::array<const char*, 4> aliases;
  AdBlockResourceType type;
  const char* mime_type;
  const char* base64_body;
  const char* version;
  const char* sha256;
};

constexpr BundledResource kBundledResources[] = {
    {
        "seoul-noop.js",
        {"noop.js", "noopjs"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKfSkoKTsK",
        "1",
        "a6c40a75a40cb29ddd1046bf271a6a5e4480129680f4752de3b1d3f83c25bdf5",
    },
    {
        "seoul-empty.css",
        {"noop.css", "empty.css", "noopcss"},
        AdBlockResourceType::kMime,
        "text/css",
        "LyogU2VvdWwgdmV0dGVkIGVtcHR5IHN0eWxlc2hlZXQuICovCg==",
        "1",
        "e1148969b8cbba7ef848db964624ac620ad402186460097e47c04dd4c1975b2f",
    },
    {
        "seoul-transparent.gif",
        {"1x1.gif", "transparent.gif", "1x1-transparent.gif"},
        AdBlockResourceType::kMime,
        "image/gif",
        "R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=",
        "1",
        "3b7b8a4b411ddf8db9bacc2f3aabf406f8e4c0c087829b336ca331c40adfdff1",
    },
    {
        "seoul-remove-elements.js",
        {"remove-elements.js", "remove-elements"},
        AdBlockResourceType::kScriptletTemplate,
        "",
        "ZnVuY3Rpb24gc2VvdWxSZW1vdmVFbGVtZW50cyhyYXdTZWxlY3RvcikgewogIGlmICh0eXBlb2YgcmF3U2VsZWN0b3IgIT09ICJzdHJpbmciIHx8IHJhd1NlbGVjdG9yLmxlbmd0aCA9PT0gMCB8fAogICAgICByYXdTZWxlY3Rvci5sZW5ndGggPiA1MTIgfHwgcmF3U2VsZWN0b3IuaW5jbHVkZXMoIlwwIikpIHsKICAgIHJldHVybjsKICB9CiAgbGV0IG5vZGVzOwogIHRyeSB7CiAgICBub2RlcyA9IGRvY3VtZW50LnF1ZXJ5U2VsZWN0b3JBbGwocmF3U2VsZWN0b3IpOwogIH0gY2F0Y2ggewogICAgcmV0dXJuOwogIH0KICBjb25zdCBsaW1pdCA9IE1hdGgubWluKG5vZGVzLmxlbmd0aCwgMjU2KTsKICBmb3IgKGxldCBpbmRleCA9IDA7IGluZGV4IDwgbGltaXQ7ICsraW5kZXgpIHsKICAgIG5vZGVzW2luZGV4XS5yZW1vdmUoKTsKICB9Cn0K",
        "1",
        "ee9cb53227f1bc23ae02ba2d590ea45a0c8b3d9679fdb150b783240f8d2daf6b",
    },
    {
        "seoul-remove-attr.js",
        {"remove-attr.js", "ra.js"},
        AdBlockResourceType::kScriptletTemplate,
        "",
        "ZnVuY3Rpb24gc2VvdWxSZW1vdmVBdHRyKHJhd0F0dHIsIHJhd1NlbGVjdG9yKSB7CiAgaWYgKHR5"
        "cGVvZiByYXdBdHRyICE9PSAic3RyaW5nIiB8fCByYXdBdHRyLmxlbmd0aCA9PT0gMCB8fAogICAg"
        "ICByYXdBdHRyLmxlbmd0aCA+IDEyOCB8fCByYXdBdHRyLmluY2x1ZGVzKCJcMCIpKSB7CiAgICBy"
        "ZXR1cm47CiAgfQogIGNvbnN0IHNlbGVjdG9yID0gdHlwZW9mIHJhd1NlbGVjdG9yID09PSAic3Ry"
        "aW5nIiAmJiByYXdTZWxlY3Rvci5sZW5ndGggPiAwID8KICAgICAgcmF3U2VsZWN0b3IgOiAiWyIg"
        "KyBDU1MuZXNjYXBlKHJhd0F0dHIpICsgIl0iOwogIGlmIChzZWxlY3Rvci5sZW5ndGggPiA1MTIg"
        "fHwgc2VsZWN0b3IuaW5jbHVkZXMoIlwwIikpIHsKICAgIHJldHVybjsKICB9CiAgbGV0IG5vZGVz"
        "OwogIHRyeSB7CiAgICBub2RlcyA9IGRvY3VtZW50LnF1ZXJ5U2VsZWN0b3JBbGwoc2VsZWN0b3Ip"
        "OwogIH0gY2F0Y2ggewogICAgcmV0dXJuOwogIH0KICBjb25zdCBsaW1pdCA9IE1hdGgubWluKG5v"
        "ZGVzLmxlbmd0aCwgMjU2KTsKICBmb3IgKGxldCBpbmRleCA9IDA7IGluZGV4IDwgbGltaXQ7ICsr"
        "aW5kZXgpIHsKICAgIG5vZGVzW2luZGV4XS5yZW1vdmVBdHRyaWJ1dGUocmF3QXR0cik7CiAgfQp9"
        "Cg==",
        "1",
        "10a5a8711e95f5bb08871bd6f5d76618360a5e5c76dee6ec0d41b19027a15e62",
    },
    {
        "seoul-remove-class.js",
        {"remove-class.js", "rc.js"},
        AdBlockResourceType::kScriptletTemplate,
        "",
        "ZnVuY3Rpb24gc2VvdWxSZW1vdmVDbGFzcyhyYXdDbGFzcywgcmF3U2VsZWN0b3IpIHsKICBpZiAo"
        "dHlwZW9mIHJhd0NsYXNzICE9PSAic3RyaW5nIiB8fCByYXdDbGFzcy5sZW5ndGggPT09IDAgfHwK"
        "ICAgICAgcmF3Q2xhc3MubGVuZ3RoID4gMTI4IHx8IHJhd0NsYXNzLmluY2x1ZGVzKCJcMCIpKSB7"
        "CiAgICByZXR1cm47CiAgfQogIGNvbnN0IHNlbGVjdG9yID0gdHlwZW9mIHJhd1NlbGVjdG9yID09"
        "PSAic3RyaW5nIiAmJiByYXdTZWxlY3Rvci5sZW5ndGggPiAwID8KICAgICAgcmF3U2VsZWN0b3Ig"
        "OiAiLiIgKyBDU1MuZXNjYXBlKHJhd0NsYXNzKTsKICBpZiAoc2VsZWN0b3IubGVuZ3RoID4gNTEy"
        "IHx8IHNlbGVjdG9yLmluY2x1ZGVzKCJcMCIpKSB7CiAgICByZXR1cm47CiAgfQogIGxldCBub2Rl"
        "czsKICB0cnkgewogICAgbm9kZXMgPSBkb2N1bWVudC5xdWVyeVNlbGVjdG9yQWxsKHNlbGVjdG9y"
        "KTsKICB9IGNhdGNoIHsKICAgIHJldHVybjsKICB9CiAgY29uc3QgbGltaXQgPSBNYXRoLm1pbihu"
        "b2Rlcy5sZW5ndGgsIDI1Nik7CiAgZm9yIChsZXQgaW5kZXggPSAwOyBpbmRleCA8IGxpbWl0OyAr"
        "K2luZGV4KSB7CiAgICBub2Rlc1tpbmRleF0uY2xhc3NMaXN0LnJlbW92ZShyYXdDbGFzcyk7CiAg"
        "fQp9Cg==",
        "1",
        "c6e52ed6a335b048cbae4b898331665b1c62544ad2b31cec5a69d0d2f6c8e5a5",
    },
    {
        "seoul-set-attr.js",
        {"set-attr.js", "set-attr"},
        AdBlockResourceType::kScriptletTemplate,
        "",
        "ZnVuY3Rpb24gc2VvdWxTZXRBdHRyKHJhd1NlbGVjdG9yLCByYXdBdHRyLCByYXdWYWx1ZSkgewog"
        "IGlmICh0eXBlb2YgcmF3U2VsZWN0b3IgIT09ICJzdHJpbmciIHx8IHJhd1NlbGVjdG9yLmxlbmd0"
        "aCA9PT0gMCB8fAogICAgICByYXdTZWxlY3Rvci5sZW5ndGggPiA1MTIgfHwgcmF3U2VsZWN0b3Iu"
        "aW5jbHVkZXMoIlwwIikgfHwKICAgICAgdHlwZW9mIHJhd0F0dHIgIT09ICJzdHJpbmciIHx8IHJh"
        "d0F0dHIubGVuZ3RoID09PSAwIHx8CiAgICAgIHJhd0F0dHIubGVuZ3RoID4gMTI4IHx8IHJhd0F0"
        "dHIuaW5jbHVkZXMoIlwwIikpIHsKICAgIHJldHVybjsKICB9CiAgY29uc3QgdmFsdWUgPSB0eXBl"
        "b2YgcmF3VmFsdWUgPT09ICJzdHJpbmciID8gcmF3VmFsdWUgOiAiIjsKICBpZiAodmFsdWUubGVu"
        "Z3RoID4gNTEyIHx8IHZhbHVlLmluY2x1ZGVzKCJcMCIpKSB7CiAgICByZXR1cm47CiAgfQogIGxl"
        "dCBub2RlczsKICB0cnkgewogICAgbm9kZXMgPSBkb2N1bWVudC5xdWVyeVNlbGVjdG9yQWxsKHJh"
        "d1NlbGVjdG9yKTsKICB9IGNhdGNoIHsKICAgIHJldHVybjsKICB9CiAgY29uc3QgbGltaXQgPSBN"
        "YXRoLm1pbihub2Rlcy5sZW5ndGgsIDI1Nik7CiAgZm9yIChsZXQgaW5kZXggPSAwOyBpbmRleCA8"
        "IGxpbWl0OyArK2luZGV4KSB7CiAgICB0cnkgewogICAgICBub2Rlc1tpbmRleF0uc2V0QXR0cmli"
        "dXRlKHJhd0F0dHIsIHZhbHVlKTsKICAgIH0gY2F0Y2ggewogICAgICByZXR1cm47CiAgICB9CiAg"
        "fQp9Cg==",
        "1",
        "e76f1db524e3c308434ddeb10b3ac6ab54fde598d84b1e051cbac33e14033599",
    },
    {
        "seoul-noop.txt",
        {"noop.txt", "nooptext"},
        AdBlockResourceType::kMime,
        "text/plain",
        """",
        "1",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    {
        "seoul-noop.html",
        {"noop.html", "noopframe"},
        AdBlockResourceType::kMime,
        "text/html",
        "PCFET0NUWVBFIGh0bWw+Cg==",
        "1",
        "3d0b6f5e309df8fce8815f908dd6821b0496cf43cd9fb4ff7ea9c0ce74770826",
    },
    {
        "seoul-ga.js",
        {"google-analytics.com/ga.js", "ga.js", "google-analytics_ga.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICBjb25zdCBub29wID0gZnVuY3Rpb24oKSB7"
        "fTsKICBjb25zdCB0cmFja2VyID0gewogICAgX2dldE5hbWU6IG5vb3AsIF9nZXRBY2NvdW50OiBu"
        "b29wLCBfZ2V0VmlzaXRvckN1c3RvbVZhcjogbm9vcCwKICAgIF9zZXRBY2NvdW50OiBub29wLCBf"
        "c2V0Q3VzdG9tVmFyOiBub29wLCBfc2V0RG9tYWluTmFtZTogbm9vcCwKICAgIF90cmFja0V2ZW50"
        "OiBub29wLCBfdHJhY2tQYWdldmlldzogbm9vcCwgX2xpbms6IG5vb3AsCiAgfTsKICBjb25zdCBn"
        "YXQgPSB7CiAgICBfYW5vbnltaXplSXA6IG5vb3AsCiAgICBfY3JlYXRlVHJhY2tlcjogZnVuY3Rp"
        "b24oKSB7IHJldHVybiB0cmFja2VyOyB9LAogICAgX2dldFRyYWNrZXI6IGZ1bmN0aW9uKCkgeyBy"
        "ZXR1cm4gdHJhY2tlcjsgfSwKICAgIF9nZXRUcmFja2VyQnlOYW1lOiBmdW5jdGlvbigpIHsgcmV0"
        "dXJuIHRyYWNrZXI7IH0sCiAgICBfZm9yY2VTU0w6IG5vb3AsCiAgfTsKICBjb25zdCBnYXEgPSB7"
        "CiAgICBwdXNoOiBmdW5jdGlvbihlbnRyeSkgewogICAgICBpZiAodHlwZW9mIGVudHJ5ID09PSAn"
        "ZnVuY3Rpb24nKSB7CiAgICAgICAgdHJ5IHsgZW50cnkoKTsgfSBjYXRjaCB7fQogICAgICB9CiAg"
        "ICAgIHJldHVybiAwOwogICAgfSwKICB9OwogIGNvbnN0IGV4aXN0aW5nID0gd2luZG93Ll9nYXEg"
        "JiYgd2luZG93Ll9nYXEuc2xpY2UgPyB3aW5kb3cuX2dhcSA6IFtdOwogIHdpbmRvdy5fZ2F0ID0g"
        "d2luZG93Ll9nYXQgfHwgZ2F0OwogIHdpbmRvdy5fZ2FxID0gZ2FxOwogIGV4aXN0aW5nLmZvckVh"
        "Y2goZnVuY3Rpb24oZW50cnkpIHsgZ2FxLnB1c2goZW50cnkpOyB9KTsKfSkoKTsK",
        "1",
        "9b2069c4d8107bf5a47943a7270d02170ffbe8e9514ab26a309134ccb453f36a",
    },
    {
        "seoul-analytics.js",
        {"google-analytics.com/analytics.js", "analytics.js",
         "google-analytics_analytics.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICBjb25zdCBnYSA9IGZ1bmN0aW9uKCkgewog"
        "ICAgY29uc3QgY2FsbGJhY2sgPSBhcmd1bWVudHMubGVuZ3RoICYmCiAgICAgICAgdHlwZW9mIGFy"
        "Z3VtZW50c1thcmd1bWVudHMubGVuZ3RoIC0gMV0gPT09ICdvYmplY3QnICYmCiAgICAgICAgYXJn"
        "dW1lbnRzW2FyZ3VtZW50cy5sZW5ndGggLSAxXSAhPT0gbnVsbCA/CiAgICAgICAgICAgIGFyZ3Vt"
        "ZW50c1thcmd1bWVudHMubGVuZ3RoIC0gMV0uaGl0Q2FsbGJhY2sgOiB1bmRlZmluZWQ7CiAgICBp"
        "ZiAodHlwZW9mIGNhbGxiYWNrID09PSAnZnVuY3Rpb24nKSB7CiAgICAgIHRyeSB7IHNldFRpbWVv"
        "dXQoY2FsbGJhY2ssIDEpOyB9IGNhdGNoIHt9CiAgICB9CiAgfTsKICBnYS5jcmVhdGUgPSBmdW5j"
        "dGlvbigpIHsgcmV0dXJuIHsgZ2V0OiBmdW5jdGlvbigpIHt9LCBzZXQ6IGZ1bmN0aW9uKCkge30s"
        "CiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIHNlbmQ6IGZ1bmN0aW9uKCkge30g"
        "fTsgfTsKICBnYS5nZXRCeU5hbWUgPSBmdW5jdGlvbigpIHsgcmV0dXJuIG51bGw7IH07CiAgZ2Eu"
        "Z2V0QWxsID0gZnVuY3Rpb24oKSB7IHJldHVybiBbXTsgfTsKICBnYS5yZW1vdmUgPSBmdW5jdGlv"
        "bigpIHt9OwogIGdhLmxvYWRlZCA9IHRydWU7CiAgY29uc3QgbmFtZSA9IHdpbmRvdy5Hb29nbGVB"
        "bmFseXRpY3NPYmplY3QgfHwgJ2dhJzsKICBjb25zdCBxdWV1ZWQgPSB3aW5kb3dbbmFtZV0gJiYg"
        "d2luZG93W25hbWVdLnE7CiAgd2luZG93W25hbWVdID0gZ2E7CiAgaWYgKHF1ZXVlZCAmJiBxdWV1"
        "ZWQubGVuZ3RoKSB7CiAgICAvLyBIaXQgY2FsbGJhY2tzIHF1ZXVlZCBiZWZvcmUgdGhlIHN0dWIg"
        "bGFuZGVkIHN0aWxsIGRlc2VydmUgdG8gZmlyZS4KICAgIHF1ZXVlZC5mb3JFYWNoKGZ1bmN0aW9u"
        "KGFyZ3MpIHsgdHJ5IHsgZ2EuYXBwbHkobnVsbCwgYXJncyk7IH0gY2F0Y2gge30gfSk7CiAgfQp9"
        "KSgpOwo=",
        "1",
        "c3100e508d798b9225b575accd12cea7a90f2bbb9ed112298b75b977701a4eff",
    },
    {
        "seoul-gpt.js",
        {"googletagservices.com/gpt.js", "gpt.js", "googletagservices_gpt.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICBjb25zdCBub29wID0gZnVuY3Rpb24oKSB7"
        "fTsKICBjb25zdCBjaGFpbiA9IGZ1bmN0aW9uKCkgeyByZXR1cm4gc2xvdDsgfTsKICBjb25zdCBz"
        "bG90ID0gewogICAgYWRkU2VydmljZTogY2hhaW4sIGRlZmluZVNpemVNYXBwaW5nOiBjaGFpbiwg"
        "c2V0VGFyZ2V0aW5nOiBjaGFpbiwKICAgIGNsZWFyVGFyZ2V0aW5nOiBjaGFpbiwgc2V0Q29sbGFw"
        "c2VFbXB0eURpdjogY2hhaW4sIHNldENsaWNrVXJsOiBjaGFpbiwKICAgIGdldFNsb3RFbGVtZW50"
        "SWQ6IGZ1bmN0aW9uKCkgeyByZXR1cm4gJyc7IH0sCiAgfTsKICBjb25zdCBzZXJ2aWNlID0gewog"
        "ICAgYWRkRXZlbnRMaXN0ZW5lcjogZnVuY3Rpb24oKSB7IHJldHVybiBzZXJ2aWNlOyB9LAogICAg"
        "c2V0VGFyZ2V0aW5nOiBmdW5jdGlvbigpIHsgcmV0dXJuIHNlcnZpY2U7IH0sCiAgICBjbGVhclRh"
        "cmdldGluZzogZnVuY3Rpb24oKSB7IHJldHVybiBzZXJ2aWNlOyB9LAogICAgZW5hYmxlU2luZ2xl"
        "UmVxdWVzdDogbm9vcCwgZGlzYWJsZUluaXRpYWxMb2FkOiBub29wLCByZWZyZXNoOiBub29wLAog"
        "ICAgY29sbGFwc2VFbXB0eURpdnM6IG5vb3AsIHNldENlbnRlcmluZzogbm9vcCwKICB9OwogIGNv"
        "bnN0IGdvb2dsZXRhZyA9IHdpbmRvdy5nb29nbGV0YWcgfHwge307CiAgY29uc3QgcXVldWVkID0g"
        "Z29vZ2xldGFnLmNtZCAmJiBnb29nbGV0YWcuY21kLnNsaWNlID8gZ29vZ2xldGFnLmNtZCA6IFtd"
        "OwogIGdvb2dsZXRhZy5hcGlSZWFkeSA9IHRydWU7CiAgZ29vZ2xldGFnLmNtZCA9IHsKICAgIHB1"
        "c2g6IGZ1bmN0aW9uKGNhbGxiYWNrKSB7CiAgICAgIGlmICh0eXBlb2YgY2FsbGJhY2sgPT09ICdm"
        "dW5jdGlvbicpIHsKICAgICAgICB0cnkgeyBjYWxsYmFjaygpOyB9IGNhdGNoIHt9CiAgICAgIH0K"
        "ICAgICAgcmV0dXJuIDA7CiAgICB9LAogIH07CiAgZ29vZ2xldGFnLmRlZmluZVNsb3QgPSBmdW5j"
        "dGlvbigpIHsgcmV0dXJuIHNsb3Q7IH07CiAgZ29vZ2xldGFnLmRlZmluZU91dE9mUGFnZVNsb3Qg"
        "PSBmdW5jdGlvbigpIHsgcmV0dXJuIHNsb3Q7IH07CiAgZ29vZ2xldGFnLmRlc3Ryb3lTbG90cyA9"
        "IG5vb3A7CiAgZ29vZ2xldGFnLmRpc3BsYXkgPSBub29wOwogIGdvb2dsZXRhZy5lbmFibGVTZXJ2"
        "aWNlcyA9IG5vb3A7CiAgZ29vZ2xldGFnLnB1YmFkcyA9IGZ1bmN0aW9uKCkgeyByZXR1cm4gc2Vy"
        "dmljZTsgfTsKICBnb29nbGV0YWcuY29tcGFuaW9uQWRzID0gZnVuY3Rpb24oKSB7IHJldHVybiBz"
        "ZXJ2aWNlOyB9OwogIHdpbmRvdy5nb29nbGV0YWcgPSBnb29nbGV0YWc7CiAgcXVldWVkLmZvckVh"
        "Y2goZnVuY3Rpb24oY2FsbGJhY2spIHsgZ29vZ2xldGFnLmNtZC5wdXNoKGNhbGxiYWNrKTsgfSk7"
        "Cn0pKCk7Cg==",
        "1",
        "c8df16094b0a1df33dc5730586321b286840d8e1bf10423d275df5af1876d7bd",
    },
    {
        "seoul-adsbygoogle.js",
        {"googlesyndication.com/adsbygoogle.js", "adsbygoogle.js",
         "googlesyndication_adsbygoogle.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICBjb25zdCBhZHNieWdvb2dsZSA9IHsKICAg"
        "IGxvYWRlZDogdHJ1ZSwKICAgIHB1c2g6IGZ1bmN0aW9uKCkgeyByZXR1cm4gMDsgfSwKICB9Owog"
        "IHdpbmRvdy5hZHNieWdvb2dsZSA9IGFkc2J5Z29vZ2xlOwp9KSgpOwo=",
        "1",
        "0bef22b7cea740ab94bf5ef5ff29c01f01d8ba882f8332e4358c196bc766a17e",
    },
    {
        "seoul-apstag.js",
        {"amazon-adsystem.com/aax2/apstag.js", "apstag.js", "amazon_apstag.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICBjb25zdCBjYWxsUXVldWVkID0gZnVuY3Rp"
        "b24oY2FsbGJhY2spIHsKICAgIGlmICh0eXBlb2YgY2FsbGJhY2sgPT09ICdmdW5jdGlvbicpIHsK"
        "ICAgICAgdHJ5IHsgc2V0VGltZW91dChmdW5jdGlvbigpIHsgY2FsbGJhY2soW10pOyB9LCAxKTsg"
        "fSBjYXRjaCB7fQogICAgfQogIH07CiAgd2luZG93LmFwc3RhZyA9IHsKICAgIGluaXQ6IGZ1bmN0"
        "aW9uKCkge30sCiAgICBmZXRjaEJpZHM6IGZ1bmN0aW9uKGNvbmZpZywgY2FsbGJhY2spIHsgY2Fs"
        "bFF1ZXVlZChjYWxsYmFjayk7IH0sCiAgICBzZXREaXNwbGF5QmlkczogZnVuY3Rpb24oKSB7fSwK"
        "ICAgIHRhcmdldGluZ0tleXM6IGZ1bmN0aW9uKCkgeyByZXR1cm4gW107IH0sCiAgfTsKfSkoKTsK",
        "1",
        "39a4c9218f9cfabbe3bf5e205eb3f246f308828ec9267de3bd80d40fac30895a",
    },

    {
        "seoul-gtm.js",
        {"googletagmanager.com/gtm.js", "googletagmanager_gtm.js", "gtm.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICAvLyBHb29nbGUgVGFnIE1hbmFnZXIs"
        "IG5ldXRyYWxpc2VkIGJ1dCBub3QgYWJzZW50LiBBIHBhZ2Ugd2hvc2UgR1RNIGlzIG1lcmVs"
        "eQogIC8vIGJsb2NrZWQga2VlcHMgcHVzaGluZyBvbnRvIGEgZGF0YUxheWVyIHRoYXQgbmV2"
        "ZXIgZHJhaW5zIGFuZCBjb21tb25seSB3aXJlcwogIC8vIGl0cyBvdXRib3VuZCBsaW5rcyB0"
        "aHJvdWdoIGd0YWcncyBldmVudCBjYWxsYmFjaywgc28gdGhlIGxpbmtzIHN0b3Agd29ya2lu"
        "Zy4KICAvLyBEcmFpbmluZyB0aGUgcXVldWUgYW5kIGFsd2F5cyBpbnZva2luZyBldmVudF9j"
        "YWxsYmFjayBrZWVwcyB0aGUgcGFnZSB3aG9sZS4KICBjb25zdCBub29wID0gZnVuY3Rpb24o"
        "KSB7fTsKICBjb25zdCBkcmFpbiA9IGZ1bmN0aW9uKGVudHJ5KSB7CiAgICBpZiAoIWVudHJ5"
        "KSB7IHJldHVybjsgfQogICAgY29uc3QgY2FsbGJhY2sgPSBlbnRyeS5ldmVudF9jYWxsYmFj"
        "ayB8fCBlbnRyeS5ldmVudENhbGxiYWNrOwogICAgaWYgKHR5cGVvZiBjYWxsYmFjayA9PT0g"
        "J2Z1bmN0aW9uJykgewogICAgICB0cnkgeyBzZXRUaW1lb3V0KGNhbGxiYWNrLCAxKTsgfSBj"
        "YXRjaCB7fQogICAgfQogIH07CiAgY29uc3QgZXhpc3RpbmcgPSBBcnJheS5pc0FycmF5KHdp"
        "bmRvdy5kYXRhTGF5ZXIpID8gd2luZG93LmRhdGFMYXllciA6IFtdOwogIGNvbnN0IGxheWVy"
        "ID0geyBwdXNoOiBmdW5jdGlvbigpIHsKICAgIGZvciAobGV0IGkgPSAwOyBpIDwgYXJndW1l"
        "bnRzLmxlbmd0aDsgKytpKSB7IGRyYWluKGFyZ3VtZW50c1tpXSk7IH0KICAgIHJldHVybiAw"
        "OwogIH0gfTsKICB3aW5kb3cuZGF0YUxheWVyID0gbGF5ZXI7CiAgZXhpc3RpbmcuZm9yRWFj"
        "aChkcmFpbik7CiAgY29uc3QgZ3RhZyA9IGZ1bmN0aW9uKCkgewogICAgY29uc3QgbGFzdCA9"
        "IGFyZ3VtZW50cy5sZW5ndGggPyBhcmd1bWVudHNbYXJndW1lbnRzLmxlbmd0aCAtIDFdIDog"
        "bnVsbDsKICAgIGlmIChsYXN0ICYmIHR5cGVvZiBsYXN0ID09PSAnb2JqZWN0JykgeyBkcmFp"
        "bihsYXN0KTsgfQogIH07CiAgaWYgKHR5cGVvZiB3aW5kb3cuZ3RhZyAhPT0gJ2Z1bmN0aW9u"
        "JykgeyB3aW5kb3cuZ3RhZyA9IGd0YWc7IH0KICB3aW5kb3cuZ29vZ2xlX3RhZ19tYW5hZ2Vy"
        "ID0gd2luZG93Lmdvb2dsZV90YWdfbWFuYWdlciB8fCB7fTsKICB3aW5kb3cuZ29vZ2xlX3Rh"
        "Z19kYXRhID0gd2luZG93Lmdvb2dsZV90YWdfZGF0YSB8fCB7fTsKfSkoKTsK",
        "1",
        "1fe76d8515f9e4f487237ad7ae910a0518265ee5f81c453d2dfaef17d5cfe0f6",
    },
    {
        "seoul-google-ima.js",
        {"imasdk.googleapis.com/js/sdkloader/ima3.js", "google-ima.js", "ima3.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICAvLyBHb29nbGUgSU1BLCB0aGUgU0RL"
        "IG1vc3QgSFRNTDUgcGxheWVycyB1c2UgdG8gcmVxdWVzdCB2aWRlbyBhZHMuIEJsb2NraW5n"
        "CiAgLy8gaXQgb3V0cmlnaHQgbGVhdmVzIGEgcGxheWVyIHdhaXRpbmcgZm9yZXZlciBvbiBh"
        "biBhZCB0aGF0IG5ldmVyIHJlc29sdmVzLAogIC8vIHNvIHRoZSBjb250ZW50IG5ldmVyIHN0"
        "YXJ0cy4gVGhpcyBzdHViIGFuc3dlcnMgZXZlcnkgcmVxdWVzdCBpbW1lZGlhdGVseQogIC8v"
        "IHdpdGggIm5vIGFkIiwgd2hpY2ggaXMgdGhlIG91dGNvbWUgYSBwbGF5ZXIgYWxyZWFkeSBr"
        "bm93cyBob3cgdG8gaGFuZGxlLgogIGNvbnN0IG5vb3AgPSBmdW5jdGlvbigpIHt9OwogIGNv"
        "bnN0IGxpc3RlbmVycyA9IG5ldyBXZWFrTWFwKCk7CiAgZnVuY3Rpb24gRW1pdHRlcigpIHt9"
        "CiAgRW1pdHRlci5wcm90b3R5cGUuYWRkRXZlbnRMaXN0ZW5lciA9IGZ1bmN0aW9uKHR5cGUs"
        "IGhhbmRsZXIpIHsKICAgIGlmICh0eXBlb2YgaGFuZGxlciAhPT0gJ2Z1bmN0aW9uJykgeyBy"
        "ZXR1cm47IH0KICAgIGxldCBieVR5cGUgPSBsaXN0ZW5lcnMuZ2V0KHRoaXMpOwogICAgaWYg"
        "KCFieVR5cGUpIHsgYnlUeXBlID0ge307IGxpc3RlbmVycy5zZXQodGhpcywgYnlUeXBlKTsg"
        "fQogICAgKGJ5VHlwZVt0eXBlXSA9IGJ5VHlwZVt0eXBlXSB8fCBbXSkucHVzaChoYW5kbGVy"
        "KTsKICB9OwogIEVtaXR0ZXIucHJvdG90eXBlLnJlbW92ZUV2ZW50TGlzdGVuZXIgPSBub29w"
        "OwogIEVtaXR0ZXIucHJvdG90eXBlLmVtaXQgPSBmdW5jdGlvbih0eXBlLCBldmVudCkgewog"
        "ICAgY29uc3QgYnlUeXBlID0gbGlzdGVuZXJzLmdldCh0aGlzKTsKICAgIGNvbnN0IGhhbmRs"
        "ZXJzID0gYnlUeXBlICYmIGJ5VHlwZVt0eXBlXTsKICAgIGlmICghaGFuZGxlcnMpIHsgcmV0"
        "dXJuOyB9CiAgICBoYW5kbGVycy5zbGljZSgpLmZvckVhY2goZnVuY3Rpb24oaGFuZGxlcikg"
        "ewogICAgICB0cnkgeyBoYW5kbGVyKGV2ZW50KTsgfSBjYXRjaCB7fQogICAgfSk7CiAgfTsK"
        "ICBjb25zdCBpbWEgPSB7fTsKICBpbWEuQWREaXNwbGF5Q29udGFpbmVyID0gZnVuY3Rpb24o"
        "KSB7fTsKICBpbWEuQWREaXNwbGF5Q29udGFpbmVyLnByb3RvdHlwZS5pbml0aWFsaXplID0g"
        "bm9vcDsKICBpbWEuQWREaXNwbGF5Q29udGFpbmVyLnByb3RvdHlwZS5kZXN0cm95ID0gbm9v"
        "cDsKICBpbWEuQWRzTG9hZGVkRXZlbnQgPSB7IFR5cGU6IHsgQURTX01BTkFHRVJfTE9BREVE"
        "OiAnYWRzTWFuYWdlckxvYWRlZCcgfSB9OwogIGltYS5BZEVycm9yRXZlbnQgPSB7IFR5cGU6"
        "IHsgQURfRVJST1I6ICdhZEVycm9yJyB9IH07CiAgaW1hLkFkRXZlbnQgPSB7IFR5cGU6IHsK"
        "ICAgIEFMTF9BRFNfQ09NUExFVEVEOiAnYWxsQWRzQ29tcGxldGVkJywgQ09NUExFVEU6ICdj"
        "b21wbGV0ZScsCiAgICBDT05URU5UX1BBVVNFX1JFUVVFU1RFRDogJ2NvbnRlbnRQYXVzZVJl"
        "cXVlc3RlZCcsCiAgICBDT05URU5UX1JFU1VNRV9SRVFVRVNURUQ6ICdjb250ZW50UmVzdW1l"
        "UmVxdWVzdGVkJywKICAgIExPQURFRDogJ2xvYWRlZCcsIFNUQVJURUQ6ICdzdGFydGVkJyB9"
        "IH07CiAgaW1hLkFkc1JlcXVlc3QgPSBmdW5jdGlvbigpIHt9OwogIGltYS5BZHNSZW5kZXJp"
        "bmdTZXR0aW5ncyA9IGZ1bmN0aW9uKCkge307CiAgZnVuY3Rpb24gQWRFcnJvcihtZXNzYWdl"
        "KSB7IHRoaXMubWVzc2FnZSA9IG1lc3NhZ2U7IH0KICBBZEVycm9yLnByb3RvdHlwZS5nZXRN"
        "ZXNzYWdlID0gZnVuY3Rpb24oKSB7IHJldHVybiB0aGlzLm1lc3NhZ2U7IH07CiAgQWRFcnJv"
        "ci5wcm90b3R5cGUuZ2V0RXJyb3JDb2RlID0gZnVuY3Rpb24oKSB7IHJldHVybiAxMDA5OyB9"
        "OwogIEFkRXJyb3IucHJvdG90eXBlLnRvU3RyaW5nID0gZnVuY3Rpb24oKSB7IHJldHVybiB0"
        "aGlzLm1lc3NhZ2U7IH07CiAgaW1hLkFkRXJyb3IgPSBBZEVycm9yOwogIGltYS5BZHNMb2Fk"
        "ZXIgPSBmdW5jdGlvbigpIHt9OwogIGltYS5BZHNMb2FkZXIucHJvdG90eXBlID0gT2JqZWN0"
        "LmNyZWF0ZShFbWl0dGVyLnByb3RvdHlwZSk7CiAgaW1hLkFkc0xvYWRlci5wcm90b3R5cGUu"
        "Y29udGVudENvbXBsZXRlID0gbm9vcDsKICBpbWEuQWRzTG9hZGVyLnByb3RvdHlwZS5kZXN0"
        "cm95ID0gbm9vcDsKICBpbWEuQWRzTG9hZGVyLnByb3RvdHlwZS5nZXRTZXR0aW5ncyA9IGZ1"
        "bmN0aW9uKCkgewogICAgcmV0dXJuIHsgc2V0UGxheWVyVHlwZTogbm9vcCwgc2V0UGxheWVy"
        "VmVyc2lvbjogbm9vcCwKICAgICAgICAgICAgIHNldEF1dG9QbGF5QWRCcmVha3M6IG5vb3As"
        "IHNldExvY2FsZTogbm9vcCwKICAgICAgICAgICAgIHNldFZwYWlkTW9kZTogbm9vcCwgc2V0"
        "TnVtUmVkaXJlY3RzOiBub29wIH07CiAgfTsKICBpbWEuQWRzTG9hZGVyLnByb3RvdHlwZS5y"
        "ZXF1ZXN0QWRzID0gZnVuY3Rpb24oKSB7CiAgICAvLyBObyBhZCwgcmVwb3J0ZWQgdGhlIHdh"
        "eSBhIHJlYWwgZmFpbHVyZSBpcywgc28gdGhlIHBsYXllciByZXN1bWVzIGNvbnRlbnQuCiAg"
        "ICBjb25zdCBzZWxmID0gdGhpczsKICAgIGNvbnN0IGVycm9yID0gbmV3IEFkRXJyb3IoJ25v"
        "IGFkcycpOwogICAgc2V0VGltZW91dChmdW5jdGlvbigpIHsKICAgICAgc2VsZi5lbWl0KCdh"
        "ZEVycm9yJywgewogICAgICAgIGdldEVycm9yOiBmdW5jdGlvbigpIHsgcmV0dXJuIGVycm9y"
        "OyB9LAogICAgICAgIGdldFVzZXJSZXF1ZXN0Q29udGV4dDogZnVuY3Rpb24oKSB7IHJldHVy"
        "biB7fTsgfSwKICAgICAgICB0eXBlOiAnYWRFcnJvcicKICAgICAgfSk7CiAgICB9LCAxKTsK"
        "ICB9OwogIGltYS5zZXR0aW5ncyA9IHsgc2V0UGxheWVyVHlwZTogbm9vcCwgc2V0UGxheWVy"
        "VmVyc2lvbjogbm9vcCwKICAgICAgICAgICAgICAgICAgIHNldERpc2FibGVDdXN0b21QbGF5"
        "YmFja0ZvcklPUzEwUGx1czogbm9vcCwKICAgICAgICAgICAgICAgICAgIHNldExvY2FsZTog"
        "bm9vcCwgc2V0TnVtUmVkaXJlY3RzOiBub29wLCBzZXRWcGFpZE1vZGU6IG5vb3AgfTsKICBp"
        "bWEuVmlld01vZGUgPSB7IE5PUk1BTDogJ25vcm1hbCcsIEZVTExTQ1JFRU46ICdmdWxsc2Ny"
        "ZWVuJyB9OwogIGltYS5VaUVsZW1lbnRzID0geyBBRF9BVFRSSUJVVElPTjogJ2FkQXR0cmli"
        "dXRpb24nLCBDT1VOVERPV046ICdjb3VudGRvd24nIH07CiAgaW1hLkltYVNka1NldHRpbmdz"
        "ID0gZnVuY3Rpb24oKSB7fTsKICBpbWEuSW1hU2RrU2V0dGluZ3MuVnBhaWRNb2RlID0geyBE"
        "SVNBQkxFRDogMCwgRU5BQkxFRDogMSwgSU5TRUNVUkU6IDIgfTsKICB3aW5kb3cuZ29vZ2xl"
        "ID0gd2luZG93Lmdvb2dsZSB8fCB7fTsKICB3aW5kb3cuZ29vZ2xlLmltYSA9IGltYTsKfSko"
        "KTsK",
        "1",
        "2402fb87e721c7e429b71ec449d5d58830b6a9accd7d9dd4c25cccd1412435a8",
    },
    {
        "seoul-measurement-beacons.js",
        {"scorecardresearch.com/beacon.js", "comscore_beacon.js", "outbrain.js"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKICAvLyBBIG1lYXN1cmVtZW50IGJlYWNv"
        "biByZWR1Y2VkIHRvIHRoZSBzdXJmYWNlIGl0cyBjYWxsZXJzIHRvdWNoLiBOYW1lZAogIC8v"
        "IHNlcGFyYXRlbHkgZnJvbSB0aGUgZ2VuZXJpYyBuby1vcCBzbyBhIHJ1bGUgdGhhdCBhc2tz"
        "IGZvciB0aGlzIG9uZSBieSBuYW1lCiAgLy8gcmVzb2x2ZXMgcmF0aGVyIHRoYW4gZmFsbGlu"
        "ZyB0aHJvdWdoIHRvIGEgaGFyZCBibG9jay4KICBjb25zdCBub29wID0gZnVuY3Rpb24oKSB7"
        "fTsKICBjb25zdCBzZWxmID0gewogICAgYmVhY29uOiBub29wLCBzZXR1cDogbm9vcCwgcnVu"
        "OiBub29wLCBnZXQ6IG5vb3AsIHB1cmdlOiBub29wLAogICAgZnVuY3Rpb25zOiB7IGJlYWNv"
        "bjogbm9vcCwgZ2V0OiBub29wLCBzZXR1cDogbm9vcCwgcnVuOiBub29wIH0sCiAgICBnZXRU"
        "YWc6IGZ1bmN0aW9uKCkgeyByZXR1cm4gbnVsbDsgfSwKICAgIGdldFRhZ3M6IGZ1bmN0aW9u"
        "KCkgeyByZXR1cm4gW107IH0sCiAgfTsKICB3aW5kb3cuQ09NU0NPUkUgPSB3aW5kb3cuQ09N"
        "U0NPUkUgfHwgc2VsZjsKICB3aW5kb3cuX2NvbXNjb3JlID0gd2luZG93Ll9jb21zY29yZSB8"
        "fCBbXTsKICB3aW5kb3cuX19jbXBDYWxsYmFja3MgPSB3aW5kb3cuX19jbXBDYWxsYmFja3Mg"
        "fHwgW107CiAgd2luZG93Lk9CUiA9IHdpbmRvdy5PQlIgfHwgeyBleHRlcm46IHsKICAgIHJl"
        "c2VhcmNoV2lkZ2V0OiBub29wLCBjYWxsQ2xpY2s6IG5vb3AsIGNhbGxSZWNzOiBub29wLCBj"
        "YWxsTG9hZE1vcmU6IG5vb3AsCiAgICByZWxvYWRXaWRnZXQ6IG5vb3AsIHNldHVwQW5kUnVu"
        "OiBub29wIH0gfTsKICB3aW5kb3cucGJqcyA9IHdpbmRvdy5wYmpzIHx8IHsgcXVlOiB7IHB1"
        "c2g6IGZ1bmN0aW9uKGNhbGxiYWNrKSB7CiAgICBpZiAodHlwZW9mIGNhbGxiYWNrID09PSAn"
        "ZnVuY3Rpb24nKSB7IHRyeSB7IGNhbGxiYWNrKCk7IH0gY2F0Y2gge30gfQogICAgcmV0dXJu"
        "IDA7IH0gfSB9Owp9KSgpOwo=",
        "1",
        "9c7a357fedb816750c0d2a03a989f3cdacdff5c1ebdbcf7521d3449964753c99",
    },
    {
        "seoul-noop.json",
        {"noop.json", "noopjson"},
        AdBlockResourceType::kMime,
        "application/json",
        "e30K",
        "1",
        "ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356",
    },
    {
        "seoul-2x2.png",
        {"2x2.png", "2x2-transparent.png"},
        AdBlockResourceType::kMime,
        "image/png",
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAC0lEQVR42mNgQAcAABIAAeRV"
        "jecAAAAASUVORK5CYII=",
        "1",
        "391590d092f57b13968ea0174fda8726918550f84594de498c72482f1f2e9623",
    },
    {
        "seoul-32x32.png",
        {"32x32.png", "32x32-transparent.png"},
        AdBlockResourceType::kMime,
        "image/png",
        "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAGklEQVR42u3BAQEAAACCIP+v"
        "bkhAAQAAAO8GECAAAcm1w7EAAAAASUVORK5CYII=",
        "1",
        "ba04f531df0c7a12124750d521add77c55b16a6432d653c5559c16680dbd9f50",
    },
};

std::vector<AdBlockResource> BuildCatalog() {
  std::vector<AdBlockResource> catalog;
  catalog.reserve(std::size(kBundledResources));
  for (const BundledResource& bundled : kBundledResources) {
    std::string body;
    CHECK(base::Base64Decode(bundled.base64_body, &body));
    const std::string digest =
        base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(body)));
    CHECK_EQ(digest, bundled.sha256);

    AdBlockResource resource;
    resource.name = bundled.name;
    for (const char* alias : bundled.aliases) {
      // Defensive rather than trusting the padding: an aggregate initialiser
      // that names fewer aliases than the array holds leaves the rest null, and
      // this runs at startup where a stray dereference is a crash rather than a
      // failed test.
      if (alias != nullptr && *alias != '\0') {
        resource.aliases.push_back(alias);
      }
    }
    resource.type = bundled.type;
    resource.mime_type = bundled.mime_type;
    resource.body = std::move(body);
    resource.version = bundled.version;
    resource.sha256 = bundled.sha256;
    if (resource.type == AdBlockResourceType::kMime) {
      resource.data_url = "data:" + resource.mime_type + ";base64," +
                          std::string(bundled.base64_body);
    }
    catalog.push_back(std::move(resource));
  }
  return catalog;
}

std::optional<AdBlockResource> FindByNameOrAlias(
    std::string_view name_or_alias) {
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    if (resource.name == name_or_alias ||
        std::ranges::find(resource.aliases, name_or_alias) !=
            resource.aliases.end()) {
      return resource;
    }
  }
  return std::nullopt;
}

}  // namespace

AdBlockResource::AdBlockResource() = default;
AdBlockResource::AdBlockResource(const AdBlockResource&) = default;
AdBlockResource& AdBlockResource::operator=(const AdBlockResource&) = default;
AdBlockResource::AdBlockResource(AdBlockResource&&) = default;
AdBlockResource& AdBlockResource::operator=(AdBlockResource&&) = default;
AdBlockResource::~AdBlockResource() = default;

std::vector<AdBlockResource> GetAdBlockResourceCatalog() {
  return BuildCatalog();
}

std::string SerializeAdBlockResourceCatalog() {
  base::ListValue resources;
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    base::DictValue descriptor;
    descriptor.Set("name", resource.name);
    base::ListValue aliases;
    for (const std::string& alias : resource.aliases) {
      aliases.Append(alias);
    }
    descriptor.Set("aliases", std::move(aliases));
    if (resource.type == AdBlockResourceType::kScriptletTemplate) {
      descriptor.Set("kind", "template");
    } else {
      base::DictValue kind;
      kind.Set("mime", resource.mime_type);
      descriptor.Set("kind", std::move(kind));
    }
    descriptor.Set("content", base::Base64Encode(resource.body));
    resources.Append(std::move(descriptor));
  }

  std::optional<std::string> json = base::WriteJson(resources);
  CHECK(json);
  return std::move(*json);
}

std::optional<AdBlockResource> FindAdBlockResourceByDataUrl(
    std::string_view data_url) {
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    if (!resource.data_url.empty() && resource.data_url == data_url) {
      return resource;
    }
  }
  return std::nullopt;
}

bool ValidateAdBlockResourceArguments(std::string_view name_or_alias,
                                      base::span<const std::string> arguments) {
  const std::optional<AdBlockResource> resource =
      FindByNameOrAlias(name_or_alias);
  if (!resource) {
    return false;
  }
  if (resource->type == AdBlockResourceType::kMime) {
    return arguments.empty();
  }
  if (resource->name != "seoul-remove-elements.js" ||
      arguments.size() != 1u) {
    return false;
  }
  const std::string& selector = arguments.front();
  return !selector.empty() && selector.size() <= 512 &&
         selector.find('\0') == std::string::npos;
}

}  // namespace seoul::adblock
