#pragma once

#include "esp_err.h"

#define BAIDUBCE_MESSAGE_FORMAT "{\
\"model\":\"glm-4-flash\",\
\"messages\":[\
{\
\"role\":\"user\",\
\"content\":\"%s\"\
}\
]\
}"
#define LLM_SERVER  "open.bigmodel.cn"
#define WEB_URL_LLM "https://" LLM_SERVER "/api/paas/v4/chat/completions"
#define LLM_KEY     "xxxxxxxxxxx"

#ifdef _cplusplus
extern "c"
{
#endif



    // send to llm
    esp_err_t llm_send_request(const char *content);
    esp_err_t llm_recv_response(char **response_data);


#ifdef _cplusplus
}
#endif