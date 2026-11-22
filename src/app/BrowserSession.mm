#include "app/BrowserSession.hpp"

#import <WebKit/WebKit.h>

namespace wealthtracker::app {

void clearBankBrowserData(QStringList domains,
                          std::function<void()> completed) {
    WKWebsiteDataStore* store = [WKWebsiteDataStore defaultDataStore];
    NSSet<NSString*>* types = [WKWebsiteDataStore allWebsiteDataTypes];
    [store fetchDataRecordsOfTypes:types completionHandler:^(NSArray<WKWebsiteDataRecord*>* records) {
        NSMutableArray<WKWebsiteDataRecord*>* selected = [NSMutableArray array];
        for (WKWebsiteDataRecord* record in records) {
            const auto name = QString::fromNSString(record.displayName).toLower();
            for (const auto& domain : domains) {
                if (name == domain || name.endsWith(QLatin1Char('.') + domain)) {
                    [selected addObject:record];
                    break;
                }
            }
        }
        [store removeDataOfTypes:types forDataRecords:selected completionHandler:^{ completed(); }];
    }];
}

} // namespace wealthtracker::app
