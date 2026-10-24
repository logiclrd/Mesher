#include "mesher/repair_step_hud.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
int main() {
    try {
        if(RepairStepHudPage::firstOperationRow!=2)
            throw std::runtime_error("classifier rows must precede operation row 3");
        const std::vector<std::string> lines{
            "STEP 2 CURRENT PAUSED",
            "LATEST OP: splitEdge",
            "LATEST ARGS: a=123456 b=567890 midpoint=92345",
            "N NEXT / P PREVIOUS / C CURRENT"
        };
        const auto first=layoutRepairStepHud(lines,16,4,0);
        if(first.lines.size()!=4||first.actualScroll!=0||
           first.maxScroll<3||first.lines.front()!="STEP 2 CURRENT P")
            throw std::runtime_error("first HUD page must clip and wrap deterministic text");
        const auto second=layoutRepairStepHud(lines,16,4,3);
        if(second.lines.size()!=4||second.actualScroll!=3||
           second.lines.front()==first.lines.front())
            throw std::runtime_error("scroll must expose later exact argument substrings");
        const auto end=layoutRepairStepHud(lines,16,4,99999);
        if(end.actualScroll!=end.maxScroll||end.lines.empty())
            throw std::runtime_error("overflow scrolling must clamp within finite HUD lines");
        bool refused=false;
        try {(void)layoutRepairStepHud(lines,0,3,0);}
        catch(const std::invalid_argument&) {refused=true;}
        if(!refused)throw std::runtime_error("zero-width HUD must fail closed");
        std::cout<<"[r06-m16d2] classifier row 1/2 reserved; operation row 3; bounded scroll and arguments PASS\n";
    }catch(const std::exception& error) {
        std::cerr<<"[r06-m16d2] FAIL "<<error.what()<<"\n";
        return 1;
    }
}
