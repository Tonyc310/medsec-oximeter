#include "link_admission.h"
#include "unity.h"

#include <stddef.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_admits_the_central_its_links_allow(void)
{
    static const struct {
        const char *name;
        link_state_t links;
        link_admission_t admitted;
    } cases[] = {
        {"nobody connected", {.connected = 0u, .bonded = true}, LINK_ADMIT_ANYONE},
        {"a stranger connected", {.connected = 1u, .bonded = true}, LINK_ADMIT_GATEWAY},
        {"a stranger connected, no bond", {.connected = 1u, .bonded = false}, LINK_ADMIT_NOBODY},
        {"the gateway linked",
         {.connected = 1u, .gateway_linked = true, .bonded = true},
         LINK_ADMIT_NOBODY},
        {"both connections in use",
         {.connected = LINK_ADMISSION_CONNECTIONS, .bonded = true},
         LINK_ADMIT_NOBODY},
    };

    for (size_t i = 0u; i < (sizeof cases / sizeof cases[0]); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].admitted, link_admission(cases[i].links),
                                      cases[i].name);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_admits_the_central_its_links_allow);
    return UNITY_END();
}
