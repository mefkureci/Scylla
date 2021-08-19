#include <FunctionExport.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>
#include <tuple>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)
// Converts bool to BOOL
#define WIN_BOOL(x) x ? TRUE : FALSE

namespace py = pybind11;

std::tuple<std::wstring, std::wstring> VersionInfo();
std::tuple<uintptr_t, uintptr_t> IatSearch(uint32_t pid, uintptr_t search_start,
                                           bool advanced_search);
bool DumpPE(uint32_t pid, uintptr_t image_base, uintptr_t entrypoint_addr,
            const std::wstring &output_file,
            std::optional<std::wstring> input_file);
void IatFix(uint32_t pid, uintptr_t iat_addr, uint32_t iat_size,
            bool create_new_iat, const std::wstring &file_to_fix,
            const std::wstring &output_file);
bool RebuildPE(const std::wstring &input_file, bool remove_dos_stub,
               bool fix_pe_checksum, bool create_backup);

// Custom exception
class ScyllaException : public std::exception {
 public:
  explicit ScyllaException(const char *m) : message{m} {}
  const char *what() const noexcept override { return message.c_str(); }

  static void ThrowOnError(int error) {
    switch (error) {
      case SCY_ERROR_PROCOPEN:
        throw ScyllaException("OpenProcess failed");
      case SCY_ERROR_IATWRITE:
        throw ScyllaException("IAT write error");
      case SCY_ERROR_IATSEARCH:
        throw ScyllaException("IAT search error");
      case SCY_ERROR_IATNOTFOUND:
        throw ScyllaException("IAT not found");
      case SCY_ERROR_PIDNOTFOUND:
        throw ScyllaException("Process ID not found");
      case SCY_ERROR_SUCCESS:
        break;
      default:
        throw ScyllaException("Unknown error");
    }
  }

 private:
  std::string message{};
};

// Actual bindings
PYBIND11_MODULE(pyscylla, m) {
  m.def("version_information", &VersionInfo, R"pbdoc(
        Return Scylla's version as a tuple of `str`. The first item is "x86" or "x64" and the second item is "vX.Y.Z".
    )pbdoc");

  m.def("search_iat", &IatSearch, R"pbdoc(
        Try to find the PE's import address table in the given process's memory and return its address and size in case of success.
    )pbdoc");

  m.def("dump_pe", &DumpPE, R"pbdoc(
        Dump a PE loaded in the given process.
    )pbdoc");

  m.def("fix_iat", &IatFix, R"pbdoc(
        Fix the import table of a PE previously dumped with Scylla.
    )pbdoc");

  m.def("rebuild_pe", &RebuildPE, R"pbdoc(
        Apply minor fixes to a PE file.
    )pbdoc");

  // Register exception
  static auto scylla_exception =
      py::register_exception<ScyllaException>(m, "ScyllaException");

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}

// Wrapper for ScyllaVersionInformationW
std::tuple<std::wstring, std::wstring> VersionInfo() {
  // The string's format is of the form "Scylla x64 v0.9.8"
  constexpr size_t kSecondElemSize = sizeof("x86") - 1;
  constexpr size_t kThirdElemSize = sizeof("vX.Y.Z") - 1;
  const std::wstring version_info = ScyllaVersionInformationW();
  // Split the string
  std::vector<size_t> space_offsets{};
  for (size_t i = 0; i < version_info.length(); i++) {
    if (version_info[i] == L' ') {
      space_offsets.push_back(i);
    }
  }
  if (space_offsets.size() != 2 ||
      version_info.length() < space_offsets[1] + kThirdElemSize) {
    // Shouldn't happen
    throw ScyllaException("Internal error");
  }

  return std::make_tuple(
      version_info.substr(space_offsets[0] + 1, kSecondElemSize),
      version_info.substr(space_offsets[1] + 1, kThirdElemSize));
}

// Wrapper for ScyllaIatSearch
std::tuple<uintptr_t, uintptr_t> IatSearch(uint32_t pid, uintptr_t search_start,
                                           bool advanced_search) {
  DWORD_PTR iat_start{};
  DWORD iat_size{};
  const auto res = ScyllaIatSearch(pid, &iat_start, &iat_size,
                                   static_cast<DWORD_PTR>(search_start),
                                   WIN_BOOL(advanced_search));
  // Check result
  ScyllaException::ThrowOnError(res);

  return std::make_tuple(iat_start, iat_size);
}

// Wrapper for ScyllaDumpProcessW
bool DumpPE(uint32_t pid, uintptr_t image_base, uintptr_t entrypoint_addr,
            const std::wstring &output_file,
            std::optional<std::wstring> input_file) {
  const WCHAR *file_to_dump{};
  if (input_file.has_value()) {
    file_to_dump = input_file.value().c_str();
  }
  return ScyllaDumpProcessW(static_cast<DWORD_PTR>(pid), file_to_dump,
                            static_cast<DWORD_PTR>(image_base),
                            static_cast<DWORD_PTR>(entrypoint_addr),
                            output_file.c_str()) == TRUE;
}

// Wrapper for ScyllaIatFixAutoW
void IatFix(uint32_t pid, uintptr_t iat_addr, uint32_t iat_size,
            bool create_new_iat, const std::wstring &input_file,
            const std::wstring &output_file) {
  const auto res = ScyllaIatFixAutoW(
      static_cast<DWORD>(pid), static_cast<DWORD_PTR>(iat_addr),
      static_cast<DWORD>(iat_size), WIN_BOOL(create_new_iat),
      input_file.c_str(), output_file.c_str());
  // Check result
  ScyllaException::ThrowOnError(res);
}

// Wrapper for ScyllaRebuildFileW
bool RebuildPE(const std::wstring &input_file, bool remove_dos_stub,
               bool fix_pe_checksum, bool create_backup) {
  return ScyllaRebuildFileW(input_file.c_str(), WIN_BOOL(remove_dos_stub),
                            WIN_BOOL(fix_pe_checksum),
                            WIN_BOOL(create_backup)) == TRUE;
}

// Needed by ScyllaLib
HINSTANCE hDllModule = nullptr;
bool IsDllMode = false;
int InitializeGui(HINSTANCE hInstance, LPARAM param) { return 0; }
