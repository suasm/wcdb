/*
 * Tencent is pleased to support the open source community by making
 * WCDB available.
 *
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <WCDB/Assembler.hpp>
#include <WCDB/Assertion.hpp>
#include <WCDB/FullCrawler.hpp>
#include <WCDB/Master.hpp>
#include <WCDB/Page.hpp>
#include <WCDB/Sequence.hpp>
#include <WCDB/SequenceCrawler.hpp>

namespace WCDB {

namespace Repair {

#pragma mark - Initialize
FullCrawler::FullCrawler(const std::string &source) : Repairman(source)
{
}

#pragma mark - Repair
void FullCrawler::work()
{
    if (isEmptyDatabase()) {
        finishProgress();
        return;
    }

    if (!m_pager.initialize()) {
        setCriticalError(m_pager.getError());
        return;
    }

    //calculate score
    int leafTablePageCount = 0;
    for (int i = 1; i <= m_pager.getPageCount(); ++i) {
        Page page(i, &m_pager);
        auto pair = page.acquireType();
        if (!pair.first // treat as leaf table
            || pair.second == Page::Type::LeafTable) {
            ++leafTablePageCount;
        }
    }
    setPageWeight((double) 1.0 / leafTablePageCount);

    if (!markAsAssembling()) {
        return;
    }
    MasterCrawler(m_pager).work(this);
    markAsAssembled();
}

#pragma mark - Crawlable
void FullCrawler::onCellCrawled(const Cell &cell)
{
    if (getCriticalLevel() >= CriticalLevel::Fatal) {
        return;
    }
    assembleCell(cell);
}

bool FullCrawler::willCrawlPage(const Page &page, int)
{
    increaseProgress(getPageWeight());
    return true;
}

#pragma mark - MasterCrawlerDelegate
void FullCrawler::onMasterPageCrawled(const Page &page)
{
    increaseProgress(getPageWeight());
}

void FullCrawler::onMasterCellCrawled(const Cell &cell, const Master *master)
{
    if (getCriticalLevel() >= CriticalLevel::Fatal) {
        return;
    }
    markCellAsCounted(cell);
    if (master == nullptr) {
        //skip index/view/trigger
        return;
    }
    if (master->tableName == Sequence::tableName()) {
        SequenceCrawler(m_pager).work(master->rootpage, this);
    } else if (Master::isReservedTableName(master->tableName)) {
        Error error;
        error.level = Error::Level::Notice;
        error.setCode(Error::Code::Notice, "Repair");
        error.message = "Filter reserved table when retrieving.";
        error.infos.set("Table", master->tableName);
        Notifier::shared()->notify(error);
        return;
    } else if (assembleTable(master->tableName, master->sql)) {
        crawl(master->rootpage);
    }
}

void FullCrawler::onMasterCrawlerError()
{
    tryUpgradeCrawlerError();
}

#pragma mark - SequenceCrawlerDelegate
void FullCrawler::onSequencePageCrawled(const Page &page)
{
    increaseProgress(getPageWeight());
}

void FullCrawler::onSequenceCellCrawled(const Cell &cell,
                                        const Sequence &sequence)
{
    if (assembleSequence(sequence.name, sequence.seq)) {
        markCellAsCounted(cell);
    }
}

void FullCrawler::onSequenceCrawlerError()
{
    tryUpgradeCrawlerError();
}

} //namespace Repair

} //namespace WCDB
