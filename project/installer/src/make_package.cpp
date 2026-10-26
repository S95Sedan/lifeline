#include "package.hpp"
#include <fstream>
#include <iostream>
using namespace lifeline::setup;
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 6,
                "Usage: lifeline-package reference-data-or-recipe.json game.exe ffmpeg-license notices output.pak");
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
        files["program/lifeline.exe"] = read_file(game);
        files["program/licenses/LICENSE-ffmpeg.txt"] = read_file(license);
        files["program/licenses/third-party-notices.txt"] = read_file(notices);
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
