#include <catch2/catch_test_macros.hpp>
#include <QDate>
#include <QString>

import wt.connectors;

using namespace wealthtracker::connectors;

TEST_CASE("generic CSV import handles Russian dates, decimals, and debit signs")
{
    const ImportSource source{
        {},
        QStringLiteral("statement.csv"),
        QString::fromUtf8(
            "Дата операции;Сумма;Описание;Валюта;ID\n"
            "29.07.2026;-1 234,56;Супермаркет;RUB;tx-1\n"
            "30.07.2026;5000,00;Зарплата;РУБ;tx-2\n")
            .toUtf8(),
    };
    ImportOptions options;
    options.accountExternalId = QStringLiteral("cash");
    options.previewRowLimit = 100;

    const auto connector = makeGenericImportConnector();
    const auto preview = connector->preview(source, options);

    REQUIRE(preview);
    REQUIRE(preview->sample.transactions.size() == 2);
    CHECK(preview->sample.transactions.at(0).postingDate == QDate(2026, 7, 29));
    CHECK(preview->sample.transactions.at(0).amount == Money{-123456, QStringLiteral("RUB")});
    CHECK(preview->sample.transactions.at(0).provenance.sourceRecordId
          == QStringLiteral("tx-1"));
    CHECK(preview->sample.transactions.at(1).amount.minorUnits == 500000);
}

TEST_CASE("bank preset selection favors Alfa-specific columns")
{
    const ImportSource source{
        {},
        QStringLiteral("alfabank.csv"),
        QString::fromUtf8(
            "Альфа-Банк\n"
            "Дата операции;Сумма в валюте счета;Описание операции\n"
            "30.07.2026;-100,00;Покупка\n")
            .toUtf8(),
    };

    const auto selected = selectBestImportConnector(makeBuiltInImportConnectors(), source);

    REQUIRE(selected);
    CHECK(selected->manifest().id == QStringLiteral("alfabank-statement-v1"));
    CHECK(selected->probe(source).requiresReview);
}

TEST_CASE("OFX, QIF, and 1C statements normalize through the generic contract")
{
    const auto connector = makeGenericImportConnector();
    ImportOptions options;
    options.accountExternalId = QStringLiteral("fallback");
    options.previewRowLimit = 100;

    SECTION("OFX")
    {
        const ImportSource source{
            {},
            QStringLiteral("statement.ofx"),
            QByteArrayLiteral(
                "OFXHEADER:100\n<OFX><CURDEF>RUB<ACCTID>40817"
                "<BANKTRANLIST><STMTTRN><DTPOSTED>20260730120000"
                "<TRNAMT>-42.10<FITID>ofx-1<NAME>Coffee</STMTTRN>"
                "</BANKTRANLIST></OFX>"),
        };
        const auto preview = connector->preview(source, options);
        REQUIRE(preview);
        REQUIRE(preview->sample.transactions.size() == 1);
        CHECK(preview->sample.transactions.front().accountExternalId
              == QStringLiteral("40817"));
        CHECK(preview->sample.transactions.front().amount.minorUnits == -4210);
    }

    SECTION("QIF")
    {
        const ImportSource source{
            {},
            QStringLiteral("statement.qif"),
            QByteArrayLiteral("!Type:Bank\nD30.07.2026\nT-10,25\nPCafe\nNqif-1\n^\n"),
        };
        const auto preview = connector->preview(source, options);
        REQUIRE(preview);
        REQUIRE(preview->sample.transactions.size() == 1);
        CHECK(preview->sample.transactions.front().description == QStringLiteral("Cafe"));
    }

    SECTION("1C")
    {
        const ImportSource source{
            {},
            QStringLiteral("1c.txt"),
            QString::fromUtf8(
                "1CClientBankExchange\n"
                "СекцияДокумент=Платежное поручение\n"
                "Номер=7\nДата=30.07.2026\nСумма=99,50\n"
                "ПлательщикСчет=40817\nНазначениеПлатежа=Комиссия\n"
                "КонецДокумента\n")
                .toUtf8(),
        };
        const auto preview = connector->preview(source, options);
        REQUIRE(preview);
        REQUIRE(preview->sample.transactions.size() == 1);
        CHECK(preview->sample.transactions.front().externalId == QStringLiteral("7"));
    }
}

TEST_CASE("malformed XLSX and reviewed PDF imports fail explicitly")
{
    const auto connector = makeGenericImportConnector();
    const ImportOptions options;

    const ImportSource xlsx{
        {}, QStringLiteral("statement.xlsx"), QByteArray("PK\x03\x04" "data", 8)};
    const auto parsedXlsx = connector->parse(xlsx, options);
    REQUIRE_FALSE(parsedXlsx);
    CHECK(parsedXlsx.error().code == ErrorCode::ParseFailed);

    const ImportSource pdf{{}, QStringLiteral("statement.pdf"), QByteArrayLiteral("%PDF-1.7")};
    const auto parsedPdf = connector->parse(pdf, options);
    REQUIRE_FALSE(parsedPdf);
    CHECK(parsedPdf.error().code == ErrorCode::UnsupportedFormat);
}
