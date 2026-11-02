// SPDX-License-Identifier: GPL-3.0-only
#include "engraving/engravingproject.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/infrastructure/mscwriter.h"
#include "global/internal/baseapplication.h"
#include "global/io/buffer.h"
#include "scoredocument.h"
namespace jianpu
{
std::shared_ptr<mu::engraving::EngravingProject> ScoreDocument::cloneProject()
{
    using namespace mu::engraving;
    auto app = muse::modularity::globalIoc()->resolve<muse::IApplication>("jianpu");
    auto clone = EngravingProject::create(app->contexts().front());
    clone->setFileInfoProvider(m_project->fileInfoProvider());
    auto buffer = muse::io::Buffer::opened(muse::io::IODevice::WriteOnly);
    MscWriter::Params wp;
    wp.device = &buffer;
    wp.filePath = "score.mscz";
    MscWriter writer(wp);
    if (!writer.open() || !m_project->writeMscz(writer, false))
    {
        fail(tr("无法复制曲谱以完成编辑事务"));
        return {};
    }
    writer.close();
    auto data = buffer.data();
    muse::io::Buffer input(&data);
    input.open(muse::io::IODevice::ReadOnly);
    MscReader::Params rp;
    rp.device = &input;
    rp.filePath = "score.mscz";
    MscReader reader(rp);
    if (!reader.open() || !clone->loadMscz(reader, nullptr, false) ||
        !clone->setupMasterScore(false))
    {
        fail(tr("无法恢复曲谱编辑事务；原始曲谱未修改"));
        return {};
    }
    return clone;
}
} // namespace jianpu
