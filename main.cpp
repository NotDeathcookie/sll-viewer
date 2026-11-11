#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;

// Global variables
inline std::vector<int> g_sllLevels;
inline bool g_isSLLSearch = false;
inline int g_sllPage = 0; // Tracks our custom page internally (0-indexed)
inline bool g_sllDataLoading = false;
inline bool g_sllDataLoadFailed = false;

// Live SLL API (same backend as the ILL, selected with listType=SLL, so the
// level list is always current with no manual data update needed).
constexpr const char* SLL_API_LEVELS_URL = "https://api.impossiblelevels.com/api/Levels/details";
constexpr const char* SLL_LIST_TYPE = "SLL";
constexpr int SLL_API_PAGE_SIZE = 100;

// Helper function to extract EXACTLY 10 level IDs for the current page
std::string getChunkedIDString(const std::vector<int>& ids, int pageIndex) {
    size_t start = static_cast<size_t>(pageIndex) * 10;
    if (start >= ids.size()) return "";

    std::stringstream ss;
    // Only grab 10 IDs so it perfectly matches one Geometry Dash page
    for (size_t i = start; i < start + 10 && i < ids.size(); ++i) {
        if (i > start) ss << ",";
        ss << ids[i];
    }
    return ss.str();
}

// Extracts a single level's numeric ID from one API entry, exactly like the
// old file-based parser did (same JSON shape: entry.level.levelId).
inline int extractLevelIdFromEntry(matjson::Value const& entry) {
    int extractedID = 0;

    if (entry.contains("level")) {
        auto levelData = entry["level"];
        if (levelData.contains("levelId") && !levelData["levelId"].isNull()) {
            auto idField = levelData["levelId"];

            if (idField.isString()) {
                std::string idStr = idField.asString().unwrapOr("0");
                if (!idStr.empty() && idStr != "0") {
                    try {
                        extractedID = std::stoi(idStr);
                    } catch (...) {
                        extractedID = 0;
                    }
                }
            } else if (idField.isNumber()) {
                extractedID = idField.asInt().unwrapOr(0);
            }
        }
    }

    return extractedID;
}

// Fetches every page of the live SLL list from the API (paginated the same
// way ILL_data_grabber.py does it: page/pageSize params, stop on a partial
// page). Only replaces the in-memory list once the ENTIRE fetch has
// succeeded, so a failed refresh never wipes out a previously loaded list.
arc::Future<> fetchAllSLLLevelsAsync() {
    g_sllDataLoading = true;
    g_sllDataLoadFailed = false;

    std::vector<int> fetchedLevels;

    int page = 1;
    while (true) {
        auto req = web::WebRequest();
        req.userAgent("Mozilla/5.0 (compatible; SLL-Viewer-GeodeMod/1.0)");
        req.param("page", page);
        req.param("pageSize", SLL_API_PAGE_SIZE);
        req.param("listType", SLL_LIST_TYPE);
        req.param("showHidden", "false");
        req.param("sortBy", "rank");
        req.param("sortDirection", "asc");

        auto res = co_await req.get(SLL_API_LEVELS_URL);

        if (res.code() < 200 || res.code() >= 300) {
            log::error("SLL Mod: API returned HTTP {} on page {}", res.code(), page);
            g_sllDataLoadFailed = true;
            break;
        }

        auto jsonRes = res.json();
        if (!jsonRes) {
            log::error("SLL Mod: Failed to parse API response as JSON on page {}", page);
            g_sllDataLoadFailed = true;
            break;
        }

        auto root = jsonRes.unwrap();
        if (!root.contains("data")) {
            log::error("SLL Mod: API response missing 'data' field on page {}", page);
            g_sllDataLoadFailed = true;
            break;
        }

        auto arrRes = root["data"].asArray();
        if (!arrRes) {
            log::error("SLL Mod: 'data' field was not an array on page {}", page);
            g_sllDataLoadFailed = true;
            break;
        }

        auto entries = arrRes.unwrap();
        int countThisPage = static_cast<int>(entries.size());

        for (auto& entry : entries) {
            int extractedID = extractLevelIdFromEntry(entry);
            if (extractedID > 0) {
                fetchedLevels.push_back(extractedID);
            }
        }

        log::info("SLL Mod: Fetched page {} ({} entries, {} valid IDs so far)", page, countThisPage, fetchedLevels.size());

        if (countThisPage < SLL_API_PAGE_SIZE) {
            // Partial (or empty) page means we've reached the end of the list.
            break;
        }
        page++;
    }

    if (!g_sllDataLoadFailed) {
        g_sllLevels = std::move(fetchedLevels);
    } else {
        log::error("SLL Mod: Live fetch failed, keeping previously loaded data ({} entries).", g_sllLevels.size());
    }

    g_sllDataLoading = false;
    log::info("SLL Mod: Finished. {} total valid level IDs in memory.", g_sllLevels.size());
}

void startSLLFetch() {
    if (g_sllDataLoading) return;
    async::spawn(fetchAllSLLLevelsAsync(), [] {});
}

// Kick off the live fetch on mod load.
$on_mod(Loaded) {
    startSLLFetch();
}

// Function to open the SLL viewer on the actively tracked page
void openSLLBrowser(bool isPush) {
    if (g_sllLevels.empty()) {
        if (g_sllDataLoading) {
            FLAlertLayer::create("Please Wait", "Still loading the SLL list from the server, try again in a moment.", "OK")->show();
        } else if (g_sllDataLoadFailed) {
            FLAlertLayer::create("Error", "Couldn't load the SLL list (check your internet connection), retrying now.", "OK")->show();
            startSLLFetch();
        } else {
            FLAlertLayer::create("Error", "No levels found.", "OK")->show();
        }
        return;
    }

    g_isSLLSearch = true;
    std::string searchString = getChunkedIDString(g_sllLevels, g_sllPage);
    auto searchObj = GJSearchObject::create(SearchType::Type19, searchString);
    
    // IMPORTANT: Lock the native search page at 0 so the GD server doesn't 
    // skip any of our exact 10 IDs!
    searchObj->m_page = 0;

    auto scene = LevelBrowserLayer::scene(searchObj);
    
    if (isPush) {
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.5f, scene));
    } else {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
}

// Hook LevelBrowserLayer to handle pagination completely manually
class $modify(SLLBrowserLayer, LevelBrowserLayer) {
    bool init(GJSearchObject* searchObj) {
        if (!LevelBrowserLayer::init(searchObj)) return false;
        
        if (g_isSLLSearch) {
            if (auto title = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("title-label"))) {
                title->setString("SILENT LEVEL LIST");
                title->setScale(0.7f); 
            }
        }
        return true;
    }

    void setupPageInfo(gd::string info, const char* key) {
        // Let the base class do its thing first
        LevelBrowserLayer::setupPageInfo(info, key);

        if (g_isSLLSearch && !g_sllLevels.empty()) {
            int maxPages = (g_sllLevels.size() + 9) / 10;
            
            // Format accurate totals (e.g., "1 to 10 of N")
            int start = (g_sllPage * 10) + 1;
            int end = std::min(static_cast<int>((g_sllPage + 1) * 10), static_cast<int>(g_sllLevels.size()));
            std::string pageText = fmt::format("{} to {} of {}", start, end, g_sllLevels.size());
            
            if (m_countText) {
                m_countText->setString(pageText.c_str());
            }

            // Manually manage arrow visibility because native m_page is locked at 0
            if (m_leftArrow) m_leftArrow->setVisible(g_sllPage > 0);
            if (m_rightArrow) m_rightArrow->setVisible(g_sllPage < maxPages - 1);
        }
    }

    void onNextPage(cocos2d::CCObject* sender) {
        if (g_isSLLSearch) {
            int maxPages = (g_sllLevels.size() + 9) / 10;
            if (g_sllPage + 1 < maxPages) {
                g_sllPage++; // Increase our custom page tracker
                openSLLBrowser(false); // Reload the layer with the next 10 IDs
            }
            return;
        }
        LevelBrowserLayer::onNextPage(sender);
    }

    void onPrevPage(cocos2d::CCObject* sender) {
        if (g_isSLLSearch) {
            if (g_sllPage > 0) {
                g_sllPage--; // Decrease our custom page tracker
                openSLLBrowser(false); // Reload the layer with the previous 10 IDs
            }
            return;
        }
        LevelBrowserLayer::onPrevPage(sender);
    }

    void onBack(CCObject* sender) {
        if (g_isSLLSearch) {
            g_isSLLSearch = false; // Safely disable our mod's hooks when exiting
        }
        LevelBrowserLayer::onBack(sender);
    }
};

// Hook LevelSearchLayer to add the purple-tinted Extreme Demon button
class $modify(SLLLevelSearchLayer, LevelSearchLayer) {
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;
            
        auto menu = this->getChildByIDRecursive("other-filter-menu");
        if (!menu) menu = this->getChildByIDRecursive("other-filters-menu");
        
        if (menu) {
            auto ccMenu = typeinfo_cast<CCMenu*>(menu);
            if (ccMenu) {
                // Load the built-in Extreme Demon sprite and tint it purple
                auto spr = CCSprite::createWithSpriteFrameName("difficulty_10_btn2_001.png");
                spr->setColor({170, 90, 255}); 
                spr->setScale(0.85f);
                
                auto btn = CCMenuItemSpriteExtra::create(
                    spr,
                    this,
                    menu_selector(SLLLevelSearchLayer::onSLLButton)
                );
                
                btn->setID("sll-button"_spr);
                ccMenu->addChild(btn);
                ccMenu->updateLayout();
            }
        }
        return true;
    }
    
    void onSLLButton(CCObject*) {
        g_sllPage = 0; // Reset tracker to page 1
        openSLLBrowser(true);
    }
};
