module;

#include <QNetworkRequest>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <expected>

module wt.connectors;

namespace wealthtracker::connectors {

HostAllowlist::HostAllowlist(QStringList allowedHosts)
{
    for (auto &host : allowedHosts) {
        host = host.trimmed().toLower();
        if (!host.isEmpty()) {
            hosts_.insert(host);
        }
    }
}

bool HostAllowlist::permits(const QUrl &url) const
{
    if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || !url.userInfo().isEmpty() || url.host().isEmpty()) {
        return false;
    }
    if (url.port(-1) != -1 && url.port() != 443) {
        return false;
    }
    return hosts_.contains(url.host().toLower());
}

ConnectorResult<QNetworkRequest> HostAllowlist::makeRequest(const QUrl &url) const
{
    if (!permits(url)) {
        return std::unexpected(ConnectorError{
            ErrorCode::SecurityViolation,
            QStringLiteral("The connector refused a URL outside its HTTPS host allowlist"),
            url.toDisplayString(QUrl::RemoveUserInfo), false});
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("WealthTracker/0.1 (local desktop client)"));
    request.setTransferTimeout(30'000);
    return request;
}

QStringList HostAllowlist::allowedHosts() const
{
    QStringList result;
    result.reserve(hosts_.size());
    for (const auto &host : hosts_) {
        result.push_back(host);
    }
    std::ranges::sort(result);
    return result;
}

} // namespace wealthtracker::connectors
