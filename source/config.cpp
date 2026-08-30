#include "config.h"
#include "wifi.h"
#include <3ds.h>
#include <string>
#include <sstream>

extern FS_Archive sdmcArchive;

extern "C" {
#include "file.h"
}

namespace config
{
    Settings current;

    static std::string configPath()
    {
        return std::string(wifi::workingDir()) + "/config.ini";
    }

    void apply()
    {
        lang::setLanguage(current.langId);
        theme::apply(current.themeId);
    }

    void load()
    {
        std::string path = configPath();
        Handle h;
        if (R_FAILED(openFile(path.c_str(), sdmcArchive, &h, false)))
        {
            apply();
            return;
        }

        u64 size = 0;
        FSFILE_GetSize(h, &size);
        if (size > 0 && size < 4096)
        {
            std::string content(size, '\0');
            u32 bytesRead = 0;
            FSFILE_Read(h, &bytesRead, 0, &content[0], size);
            content.resize(bytesRead);

            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line))
            {
                size_t eq = line.find('=');
                if (eq != std::string::npos)
                {
                    std::string key = line.substr(0, eq);
                    std::string val = line.substr(eq + 1);
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
                    while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();

                    if (key == "language")
                    {
                        int id = std::atoi(val.c_str());
                        if (id >= 0 && id < static_cast<int>(lang::Id::COUNT))
                            current.langId = static_cast<lang::Id>(id);
                    }
                    else if (key == "theme")
                    {
                        int t = std::atoi(val.c_str());
                        if (t >= 0 && t <= 2)
                            current.themeId = static_cast<theme::Type>(t);
                    }
                }
            }
        }
        FSFILE_Close(h);
        apply();
    }

    void save()
    {
        std::string dir = wifi::workingDir();
        createFolderRecursively((dir + "/").c_str(), sdmcArchive);

        std::string path = configPath();
        Handle h;
        if (R_FAILED(openFile(path.c_str(), sdmcArchive, &h, true))) return;

        std::string content = "[config]\n";
        content += "language=" + std::to_string(static_cast<int>(current.langId)) + "\n";
        content += "theme=" + std::to_string(static_cast<int>(current.themeId)) + "\n";

        u32 bytesWritten = 0;
        FSFILE_Write(h, &bytesWritten, 0, content.data(), content.size(), FS_WRITE_FLUSH);
        FSFILE_Close(h);
    }
}
