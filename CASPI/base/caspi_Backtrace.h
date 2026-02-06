#pragma once
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
// Work around Windows SDK C++20 bug
#if defined(__cpp_modules)
    #define ModuleName ModuleName_
#endif

#include <windows.h>
#include <dbghelp.h>

#if defined(__cpp_modules)
    #undef ModuleName
#endif

#pragma comment(lib, "dbghelp.lib")
#else
#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>
#endif

namespace CASPI
{
    namespace Unwind
    {
        inline std::vector<std::string> capture (int max_frames = 32)
        {
            std::vector<std::string> frames;

#if defined(_WIN32)
            void* stack[64];
            USHORT frames_captured = CaptureStackBackTrace (0, max_frames, stack, nullptr);
            HANDLE process         = GetCurrentProcess();
            SymInitialize (process, nullptr, TRUE);

            for (USHORT i = 0; i < frames_captured; ++i)
            {
                DWORD64 addr                            = (DWORD64) (stack[i]);
                char buffer[sizeof (SYMBOL_INFO) + 256] = {};
                SYMBOL_INFO* symbol                     = reinterpret_cast<SYMBOL_INFO*> (buffer);
                symbol->SizeOfStruct                    = sizeof (SYMBOL_INFO);
                symbol->MaxNameLen                      = 255;
                if (SymFromAddr (process, addr, 0, symbol))
                {
                    frames.emplace_back (symbol->Name);
                }
                else
                {
                    frames.emplace_back ("unknown");
                }
            }

#else
            void* buffer[64];
            int n          = backtrace (buffer, max_frames);
            char** symbols = backtrace_symbols (buffer, n);
            if (symbols)
            {
                for (int i = 0; i < n; ++i)
                {
                    std::string name (symbols[i]);
                    // Attempt C++ demangle
                    size_t begin = name.find ('(');
                    size_t end   = name.find ('+', begin);
                    if (begin != std::string::npos && end != std::string::npos)
                    {
                        std::string mangled = name.substr (begin + 1, end - begin - 1);
                        int status          = 0;
                        char* demangled     = abi::__cxa_demangle (mangled.c_str(), nullptr, nullptr, &status);
                        if (status == 0 && demangled)
                        {
                            name.replace (begin + 1, end - begin - 1, demangled);
                            free (demangled);
                        }
                    }
                    frames.push_back (name);
                }
                free (symbols);
            }
#endif

            return frames;
        }
    } // namespace Unwind

} // namespace CASPI
