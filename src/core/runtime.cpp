// SPDX-License-Identifier: GPL-3.0-only
#include "runtime.h"
#include "draw/drawmodule.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/mscore.h"
#include "engraving/engravingmodule.h"
#include "global/internal/baseapplication.h"
#include "global/internal/cmdoptions.h"
#include "importexport/midi/midimodule.h"
#include "importexport/musicxml/musicxmlmodule.h"
#include <QCoreApplication>

namespace jianpu
{
namespace
{
class Application final : public muse::BaseApplication
{
  public:
    Application() : BaseApplication(std::make_shared<muse::CmdOptions>()) {}
    void startupScenario(const muse::modularity::ContextPtr &) override {}
};
} // namespace
EngineRuntime::EngineRuntime() : m_app(std::make_shared<Application>())
{
    m_app->addModule(new muse::draw::DrawModule);
    m_app->addModule(new mu::engraving::EngravingModule);
    m_app->addModule(new mu::iex::musicxml::MusicXmlModule);
    m_app->addModule(new mu::iex::midi::MidiModule);
    m_app->setup();
    // Error reporting is owned by our UI; importing must never open Studio dialogs.
    mu::engraving::MScore::noGui = true;
    mu::engraving::loadInstrumentTemplates(":/engraving/instruments/instruments.xml");
    m_app->setupNewContext();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}
EngineRuntime::~EngineRuntime() { m_app->finish(); }
} // namespace jianpu
