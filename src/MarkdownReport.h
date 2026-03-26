#pragma once
#include "ReportGenerator.h"
namespace connect {
class MarkdownReportGenerator : public IReportGenerator {
public:
    void generate(const ReportData& data, std::ostream& out) const override;
};
} // namespace connect
