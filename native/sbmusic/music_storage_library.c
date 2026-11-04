#include "music_storage_library.h"
#include <string.h>
sb_ml_status sb_storage_library_publish(sb_music_library *library,const sb_music_media *media,
 uint64_t id,uint64_t pending)
{
 if(!library||!media||!media->generation||!sb_music_source_equal(&media->source,&media->source))return SB_ML_INVALID;
 if(!media->online&&pending){
  sb_ml_status rc=sb_music_library_cancel(library,pending);
  if(rc==SB_ML_BUSY)return rc;
 }
 sb_ml_request request;memset(&request,0,sizeof request);request.request_id=id;request.operation=SB_ML_SOURCE;request.data.media=*media;
 return sb_music_library_submit(library,&request);
}
int sb_storage_library_result_current(const sb_storage_model *model,unsigned slot,const sb_music_media *submitted,const sb_ml_result *result)
{
 if(!result||result->status!=SB_ML_OK||!sb_storage_current(model,slot,submitted))return 0;
 if(result->operation==SB_ML_QUERY)return result->data.query.page.media_generation==submitted->generation&&result->data.query.page.media_online;
 if(result->operation==SB_ML_DETAIL)return result->data.track.media_generation==submitted->generation&&sb_music_source_equal(&result->data.track.identity.source,&submitted->source);
 return 1;
}
void sb_storage_revoke_resources(const sb_music_resource_provider *provider,const sb_music_media *previous,const sb_music_media *current)
{
 if(provider&&provider->revoke_media&&previous&&previous->online&&(!current||!sb_music_media_current(previous,current)))provider->revoke_media(&previous->source,previous->generation);
}
