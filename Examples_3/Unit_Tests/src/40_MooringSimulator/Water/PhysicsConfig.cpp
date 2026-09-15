#include "PhysicsConfig.h"
#include "Common_3/Utilities/Interfaces/IFileSystem.h"
#include "Common_3/Utilities/Interfaces/ILog.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace mooring {
bool loadPhysicsConfig(SeaState& sea,unsigned& resolution) {
    MTRACY_ZONE("loadPhysicsConfig");
    TFFileStream file={};
    if(!fsOpenStreamFromPath(TF_RD_OTHER_FILES,"physics.ini",TF_FM_READ,&file)) return false;
    char text[4096]; auto length=fsGetStreamFileSize(&file);
    if(length<0 || length>=sizeof(text)) { fsCloseStream(&file); return false; }
    bool valid=fsReadFromStream(&file,text,length)==size_t(length); fsCloseStream(&file); text[length]=0;
    struct Field { const char* name; float* value; float minimum,maximum; };
    Field fields[]={
        {"wind_speed_mps",&sea.windSpeed,0,35},{"wind_direction_rad",&sea.windDirection,-6.284f,6.284f},
        {"gust_fraction",&sea.gust,0,.7f},{"fetch_m",&sea.fetch,50,200000},{"wind_wave_height_m",&sea.windWaveHeight,0,6},
        {"swell_height_m",&sea.swellHeight,0,6},{"swell_period_s",&sea.swellPeriod,1.5f,18},
        {"swell_direction_rad",&sea.swellDirection,-6.284f,6.284f},{"depth_m",&sea.depth,.5f,100},
        {"water_level_m",&sea.level,-3,3},{"choppiness",&sea.choppiness,0,1},
        {"current_x_mps",&sea.current.x,-3,3},{"current_z_mps",&sea.current.z,-3,3}
    };
    char* line=text;
    while(line && *line) {
        char* end=strchr(line,'\n'); if(end) *end=0;
        char name[64]={}; float value;
        if(sscanf(line," %63s = %f",name,&value)==2) {
            bool found=false;
            if(!strcmp(name,"fft_resolution")) { found=true; valid&=value==256 || value==512; if(valid) resolution=unsigned(value); }
            else if(!strcmp(name,"seed")) { found=true; unsigned seed=sea.seed; valid&=sscanf(line," %63s = %u",name,&seed)==2; sea.seed=seed; }
            else for(auto& field:fields) if(!strcmp(field.name,name)) {
                found=true;
                if(!std::isfinite(value) || value<field.minimum || value>field.maximum) { valid=false; LOGF(eERROR,"Invalid physics setting: %s",name); }
                else *field.value=value;
                break;
            }
            if(!found) { valid=false; LOGF(eERROR,"Unknown physics setting: %s",name); }
        }
        line=end?end+1:nullptr;
    }
    return valid;
}
}
