#include "script/inventory.hpp"
#include "core/state_archive.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <sstream>

namespace lifeline::script {
namespace {
std::int32_t integer(float value){
    if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid inventory integer");
    return std::int32_t(value);
}
int number(const std::string& text){int value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size())throw std::runtime_error("Invalid inventory column");return value;
}
std::string unescape(const std::string& text){
    std::string result;
    for(std::size_t i=0;i<text.size();++i){
        if(text[i]!='\\'){result+=text[i];continue;}
        if(++i==text.size())throw std::runtime_error("Invalid inventory escape");
        switch(text[i]){case '\\':result+='\\';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;default:throw std::runtime_error("Invalid inventory escape");}
    }return result;
}
}
void Inventory::select(unsigned bank,const std::string& member){
    const auto& files=resources_.find_files(member);
    if(files.size()!=1 || files[0].conversion!="inventory-tsv-v1" || !files[0].bytes)throw std::runtime_error("Inventory needs installed table conversion");
    const auto& bytes=*files[0].bytes;std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string line;
    if(!std::getline(input,line) || line!="LIFELINE_INVENTORY\t1")throw std::runtime_error("Unsupported inventory table");
    std::map<int,Record> records;
    while(std::getline(input,line)){
        std::vector<std::string> fields;std::size_t begin=0;
        for(;;){const auto end=line.find('\t',begin);fields.push_back(line.substr(begin,end==std::string::npos?end:end-begin));if(end==std::string::npos)break;begin=end+1;}
        if(fields.size()!=8)throw std::runtime_error("Invalid inventory record");
        Record r;for(unsigned i=0;i<6;++i)r.fields[i]=number(fields[i]);r.label=unescape(fields[6]);r.description=unescape(fields[7]);
        if(!records.emplace(r.fields[0],std::move(r)).second)throw std::runtime_error("Duplicate inventory record");
    }
    banks_.at(bank)=std::move(records);
}
const Inventory::Record* Inventory::record(unsigned bank,int id)const{
    const auto& records=banks_.at(bank);const auto found=records.find(id);return found==records.end()?nullptr:&found->second;
}
Bytes Inventory::snapshot()const{
    StateWriter output;output.word(1);
    for(const auto& bank:banks_){output.word(unsigned(bank.size()));for(const auto& [id,record]:bank){for(auto field:record.fields)output.word(std::uint32_t(field));output.string(record.label);output.string(record.description);}}
    return output.take();
}
std::int32_t Inventory::snapshot_value(std::span<const std::uint8_t> bytes,int id,int selector){
    if(selector<1||selector>3)throw std::runtime_error("Saved inventory query requires a numeric selector");
    StateReader input(bytes);if(input.word()!=1)throw std::runtime_error("Unsupported inventory snapshot version");
    const auto count=input.word();if(count>512)throw std::runtime_error("Invalid saved inventory count");
    for(unsigned i=0;i<count;++i){std::array<std::int32_t,6> fields;for(auto& field:fields)field=std::bit_cast<std::int32_t>(input.word());input.string();input.string();
        if(fields[0]==id)return fields[selector==1?3:selector==2?1:2];}
    throw std::runtime_error("Saved inventory query has no matching record");
}
void Inventory::restore(std::span<const std::uint8_t> bytes){
    StateReader input(bytes);if(input.word()!=1)throw std::runtime_error("Unsupported inventory snapshot version");
    decltype(banks_) restored;
    for(auto& bank:restored){const auto count=input.word();if(count>512)throw std::runtime_error("Inventory snapshot exceeds original bank capacity");
        for(unsigned i=0;i<count;++i){Record record;for(auto& field:record.fields)field=std::bit_cast<std::int32_t>(input.word());record.label=input.string();record.description=input.string();
            if(!bank.emplace(record.fields[0],std::move(record)).second)throw std::runtime_error("Duplicate inventory snapshot record");}}
    if(!input.empty())throw std::runtime_error("Trailing inventory snapshot data");banks_=std::move(restored);
}
// Speech service 143594..1435c4 counts capture starts; 1436b0..143750
// records scored hypotheses separately, their sum, and the highest score.
void Inventory::speech_attempt(){
    auto& value=banks_[0].at(52).fields[2];
    value=std::bit_cast<std::int32_t>(std::uint32_t(value)+1u);
}
void Inventory::speech_score(unsigned score){
    if(score>10000)throw std::invalid_argument("Invalid speech statistic score");
    auto& count=banks_[0].at(53).fields[2];auto& sum=banks_[0].at(54).fields[2];auto& best=banks_[0].at(55).fields[2];
    count=std::bit_cast<std::int32_t>(std::uint32_t(count)+1u);
    sum=std::bit_cast<std::int32_t>(std::uint32_t(sum)+score);best=std::max(best,int(score));
}
Bindings Inventory::bindings(){
    Bindings result;
    for(unsigned opcode:{1099u,1113u})result.emplace(opcode,Binding{3,0,[this,opcode](const Invocation& c){
        const auto bank=opcode==1113?1u:0u;const auto message=integer(c.arguments[0]);
        // Generic service slot +40 is 160578 (table lifecycle), not the
        // neighboring numeric-column getter 1606c0.
        if(message==0){banks_[bank].clear();return Reply{false,1.f};}
        if(message==1){const auto address=integer(c.arguments[1]);if(address<0||unsigned(address)>=c.program.size())throw std::runtime_error("Invalid inventory table name");select(bank,c.program.string(unsigned(address)));return Reply{false,1.f};}
        if(message==2)throw std::runtime_error("Inventory raw table pointer protocol is not connected");
        return Reply{false,1.f};
    }});
    for(unsigned opcode:{1096u,1097u,1111u})result.emplace(opcode,Binding{2,0,[this,opcode](const Invocation& c){
        const int id=integer(c.arguments[0]),input=integer(c.arguments[1]);auto& bank=banks_[opcode==1111?1:0];
        const auto found=bank.find(id);if(found==bank.end())return Reply{false,-1.f};
        auto& quantity=found->second.fields[2];const auto previous=quantity;
        if(opcode==1097)quantity=input;
        else {
            quantity=std::bit_cast<std::int32_t>(std::uint32_t(quantity)+std::uint32_t(input));
            // Only 1096 caps item 7; direct assignment and keywords do not.
            if(opcode==1096 && id==7 && quantity>16)quantity=16;
        }
        return Reply{false,float(previous)};
    }});
    return result;
}
}
