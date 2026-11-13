#include "package.hpp"
#include <fstream>
#include <iostream>
using namespace lifeline::setup;
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 7,
                "Usage: lifeline-package reference-data-or-recipe.json game.exe ffmpeg-license notices output.pak linux-runtime-folder");
        fs::path reference(argv[1]), game(argv[2]), license(argv[3]), notices(argv[4]), output(argv[5]);
        const bool prepared = fs::is_regular_file(reference);
        auto manifest = read_json(prepared ? reference : reference / L"manifest.json");
        require(manifest.at("format") == 1 && manifest.at("layout_version") == 2 &&
                    manifest.at("game") == "SLUS_208.48",
                "Unsupported reference installation metadata");
        Json recipe = manifest;
        if (prepared) {
            require(recipe.at("recipe_version") == 1 && recipe.at("catalog").at("format") == 1 &&
                        recipe.at("resource_catalog").get<std::string>().starts_with("LIFELINE_RESOURCE_CATALOG\t1"),
                    "Unsupported source distribution recipe");
        } else {
            recipe["recipe_version"] = 1;
            recipe["catalog"] = read_json(reference / L"script/index.json");
            auto catalog_bytes = read_file(reference / L"resources.tsv");
            recipe["resource_catalog"] = std::string(catalog_bytes.begin(), catalog_bytes.end());
        }
        // The plan contains identities, destinations, converter settings and hashes.
        // It never includes original or converted game payloads.
        for (auto &record : recipe["assets"]) {
            safe_path(prepared ? reference.parent_path() : reference, record.at("path").get<std::string>());
            if (prepared) {
                require(record.at("sha256").get<std::string>().size() == 64 && record.at("bytes").is_number_unsigned(),
                        "Invalid source distribution asset identity");
                if (record.value("conversion", "").find("bitmap-") != std::string::npos &&
                    fs::path(record.at("path").get<std::string>()).extension() == L".bmp")
                    require(record.at("width").get<unsigned>() > 0 && record.at("height").get<unsigned>() > 0,
                            "Missing source distribution bitmap dimensions");
                continue;
            }
            auto path = safe_path(reference, record.at("path").get<std::string>());
            require(fs::is_regular_file(path) &&
                        fs::file_size(path) == record.at("bytes").get<std::uint64_t>(),
                    "Incomplete reference output: " + path_text(path));
            if (record.value("conversion", "").find("bitmap-") != std::string::npos &&
                path.extension() == L".bmp") {
                std::ifstream input(path, std::ios::binary);
                Bytes header(26);
                input.read(reinterpret_cast<char *>(header.data()), header.size());
                require(input.gcount() == std::streamsize(header.size()) && header[0] == 'B' &&
                            header[1] == 'M',
                        "Invalid reference bitmap metadata: " + path_text(path));
                record["width"] = u32(header, 18);
                record["height"] = u32(header, 22);
            }
        }
        std::map<std::string, Bytes> files;
        files["recipe.json"] = bytes(recipe.dump());
        auto windows_game=read_file(game);
        require(windows_game.size()>64&&u16(windows_game,0)==0x5a4d,"The Windows runtime must be a Windows PE executable");
        const auto pe=std::size_t(u32(windows_game,60));
        require(pe<=windows_game.size()&&windows_game.size()-pe>=24&&u32(windows_game,pe)==0x4550&&u16(windows_game,pe+4)==0x8664,
                "The Windows runtime must be a native Windows x86-64 executable");
        files["program/windows/lifeline.exe"] = std::move(windows_game);
        files["program/shared/licenses/LICENSE-ffmpeg.txt"] = read_file(license);
        files["program/shared/licenses/third-party-notices.txt"] = read_file(notices);
        const fs::path linux_root(argv[6]);
        const auto runtime=read_json(linux_root/L"runtime-manifest.json");
        const auto linux_game=read_file(linux_root/L"lifeline");
        require(linux_game.size()>20&&fixed_string(linux_game,0,4)=="\x7f" "ELF"&&linux_game[4]==2&&linux_game[5]==1&&u16(linux_game,18)==62,
                "The Steam Deck runtime must contain a native Linux x86-64 ELF executable");
        require(sha256(linux_game)==runtime.at("binary_sha256").get<std::string>(),"The Linux executable does not match its runtime manifest");
        files["program/linux/lifeline"]=linux_game;
        for(const auto* name:{"launch.sh","README.txt","runtime-manifest.json"})
            files[std::string("program/linux/")+name]=read_file(safe_path(linux_root,name));
        for(const auto& library:runtime.at("bundled_libraries")){
            const auto name=std::string("lib/")+library.at("name").get<std::string>();
            auto content=read_file(safe_path(linux_root,name));
            require(sha256(content)==library.at("sha256").get<std::string>(),"Linux runtime library failed verification: "+name);
            files["program/linux/"+name]=std::move(content);
        }
        check_tree(linux_root/L"licenses");
        for(const auto& entry:fs::recursive_directory_iterator(linux_root/L"licenses"))if(entry.is_regular_file()){
            const auto name=entry.path().lexically_relative(linux_root).generic_string();
            files["program/linux/"+name]=read_file(safe_path(linux_root,name));
        }
        Json report = {{"format", 1}, {"files", Json::array()}};
        for (const auto &[name, data] : files)
            report["files"].push_back(Json{{"path", name}, {"bytes", data.size()}, {"sha256", sha256(data)}});
        auto package = make_package(files);
        write_file(output, package);
        report["package_bytes"] = package.size();
        report["sha256"] = sha256(package);
        write_text(output.parent_path() / L"package-report.json", report.dump(2) + "\n");
        std::cout << "Native setup payload: " << package.size() << " bytes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
