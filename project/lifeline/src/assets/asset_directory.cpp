#include "assets/asset_directory.hpp"

namespace lifeline {
AssetDirectory::AssetDirectory(const std::filesystem::path& root) {
    if(!std::filesystem::is_directory(root)) throw std::runtime_error("Cannot open asset folder: "+root.string()+". Run build.bat to prepare game data.");
    root_=std::filesystem::canonical(root);
}
std::filesystem::path AssetDirectory::resolve(const std::string& logical_path) const {
    if(logical_path.empty() || logical_path.front()=='/' || logical_path.back()=='/' || logical_path.find_first_of("\\:")!=std::string::npos ||
        std::any_of(logical_path.begin(),logical_path.end(),[](unsigned char c){return c<32 || c>126;})) throw std::runtime_error("Invalid external asset path");
    std::size_t start=0;
    while(start<logical_path.size()) {
        const auto stop=logical_path.find('/',start),length=stop==std::string::npos?logical_path.size()-start:stop-start;
        const auto part=logical_path.substr(start,length);
        if(part.empty() || part=="." || part=="..") throw std::runtime_error("Noncanonical external asset path");
        if(stop==std::string::npos) break;start=stop+1;
    }
    const auto file=std::filesystem::weakly_canonical(root_/logical_path),relative=file.lexically_relative(root_);
    if(relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("External asset path leaves data folder");
    if(!std::filesystem::is_regular_file(file)) throw std::runtime_error("Missing external asset: "+logical_path);
    return file;
}
Bytes AssetDirectory::read(const std::string& logical_path) const {
    const auto file=resolve(logical_path);
    const auto size=std::filesystem::file_size(file);
    if(!size || size>512ULL*1024*1024) throw std::runtime_error("Invalid external asset size: "+logical_path);
    std::ifstream input(file,std::ios::binary);Bytes bytes(static_cast<std::size_t>(size));
    if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Cannot read external asset: "+logical_path);
    return bytes;
}
std::size_t AssetDirectory::verify_readable() const {
    std::size_t count=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root_)) if(entry.is_regular_file()) {
        const auto path=entry.path().lexically_relative(root_).generic_string();
        if(path=="manifest.json") continue;
        const auto file=resolve(path);
        std::ifstream input(file,std::ios::binary);
        if(input.peek()==std::char_traits<char>::eof()) throw std::runtime_error("Empty or unreadable external asset: "+path);
        ++count;
    }
    if(!count) throw std::runtime_error("No assets in data folder");
    return count;
}
}
