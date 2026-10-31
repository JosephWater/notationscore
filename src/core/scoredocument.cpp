// SPDX-License-Identifier: GPL-3.0-only
#include "scoredocument.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/barline.h"
#include "engraving/dom/beam.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/fermata.h"
#include "engraving/dom/hairpin.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/jump.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/marker.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/noteval.h"
#include "engraving/dom/ornament.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/tuplet.h"
#include "engraving/dom/volta.h"
#include "engraving/editing/editdata.h"
#include "engraving/editing/editkeysig.h"
#include "engraving/editing/editnote.h"
#include "engraving/editing/editpart.h"
#include "engraving/editing/edittimesig.h"
#include "engraving/editing/paste.h"
#include "engraving/editing/transaction/transaction.h"
#include "engraving/editing/transaction/undostack.h"
#include "engraving/editing/transpose.h"
#include "engraving/engravingproject.h"
#include "engraving/infrastructure/eidregister.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/internal/qmimedataadapter.h"
#include "engraving/rw/write/writecontext.h"
#include "engraving/types/symnames.h"
#include "engraving/types/typesconv.h"
#include "global/internal/baseapplication.h"
#include "global/io/buffer.h"
#include "global/serialization/zipreader.h"
#include "global/serialization/zipwriter.h"
#include "importexport/midi/internal/midiexport/exportmidi.h"
#include "importexport/midi/internal/midiimport/importmidi_operations.h"
#include "importexport/midi/internal/notationmidireader.h"
#include "importexport/musicxml/internal/export/exportmusicxml.h"
#include "importexport/musicxml/internal/import/importmusicxml.h"
#include "importexport/musicxml/internal/import/importmusicxmllogger.h"
#include "importexport/musicxml/internal/import/importmusicxmlpass1.h"
#include "importexport/musicxml/internal/import/importmusicxmlpass2.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <cmath>

using namespace mu::engraving;
namespace jianpu
{
namespace
{
constexpr auto EngineRevision = "4509c9072c583f7ab0c805ded2ab88f29e0ec0e5";
muse::modularity::ContextPtr context()
{
    auto app = muse::modularity::globalIoc()->resolve<muse::IApplication>("jianpu");
    return app->contexts().front();
}
QString idFor(const EngravingObject *item)
{
    if (!item->eid().isValid())
        item->assignNewEID();
    return QString::fromStdString(item->eid().toStdString());
}
QString retError(const muse::Ret &ret) { return QString::fromStdString(ret.toString()); }
bool atomicWrite(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
    {
        error = out.errorString();
        return false;
    }
    return true;
}
} // namespace
ScoreDocument::ScoreDocument(QObject *parent) : QObject(parent) {}
ScoreDocument::~ScoreDocument() = default;
bool ScoreDocument::fail(const QString &message)
{
    m_error = message;
    return false;
}
void ScoreDocument::notify()
{
    ++m_revision;
    m_error.clear();
    emit changed();
}

QVector<MidiTrackInfo> ScoreDocument::midiTracks(const QString &path, QString *error)
{
    QVector<MidiTrackInfo> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return result;
    }
    try
    {
        mu::iex::midi::MidiFile midi;
        midi.read(&file);
        if (midi.format() > 1 || midi.isDivisionInTps() || midi.division() <= 0)
        {
            if (error)
                *error = tr("首版支持 SMF 0／1 和 PPQ 时间制 MIDI");
            return {};
        }
        midi.separateChannel();
        int index = 0;
        for (const auto &track : midi.tracks())
        {
            QString name;
            bool hasNotes = false;
            for (const auto &[tick, event] : track.events())
            {
                Q_UNUSED(tick);
                if (event.type() == ME_META && event.metaType() == META_TRACK_NAME)
                    name = QString::fromUtf8(reinterpret_cast<const char *>(event.edata()),
                                             event.len());
                if (event.type() == ME_NOTEON && event.velo() > 0)
                    hasNotes = true;
            }
            if (hasNotes)
            {
                result.push_back({index, name.isEmpty() ? tr("轨道 %1").arg(index + 1) : name,
                                  track.drumTrack()});
                ++index;
            }
        }
    }
    catch (const QString &e)
    {
        if (error)
            *error = e;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return result;
}

bool ScoreDocument::open(const QString &path, const ImportOptions &options)
{
    m_error.clear();
    if (!QFileInfo::exists(path))
        return fail(tr("文件不存在：%1").arg(path));
    auto extension = QFileInfo(path).suffix().toLower();
    if (extension == "txt" || extension == "jptxt")
    {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly))
            return fail(input.errorString());
        auto bytes = input.readAll();
        auto text = QString::fromUtf8(bytes);
        if (text.startsWith(QChar(0xfeff)))
            text.remove(0, 1);
        text.replace("\r\n", "\n").replace('\r', '\n');
        ScoreDocument candidate;
        if (!candidate.newScore() || !candidate.applyText(text))
            return fail(candidate.error());
        m_project = candidate.m_project;
        m_input = candidate.m_input;
        m_view = {};
        m_savedView = {};
        m_path.clear();
        m_imported = true;
        m_history.clear();
        m_states = {0};
        m_cursor = 0;
        m_savedState = 0;
        m_mergeKey.clear();
        m_warnings.clear();
        notify();
        emit loaded();
        return true;
    }
    ScoreLoad loading;
    auto candidate = EngravingProject::create(context());
    auto *score = candidate->masterScore();
    candidate->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
    const auto suffix = QFileInfo(path).suffix().toLower();
    QJsonObject newView, newInput;
    QStringList newWarnings;
    bool native = suffix == "jianpu";
    try
    {
        if (native)
        {
            muse::ZipReader zip{muse::io::path_t(path)};
            if (zip.hasError() || !zip.fileExists("manifest.json") || !zip.fileExists("score.mscz"))
                return fail(tr("不是有效的简谱工程"));
            auto manifest =
                QJsonDocument::fromJson(zip.fileData("manifest.json").toQByteArray()).object();
            if (manifest.value("schemaVersion").toInt() < 1 ||
                manifest.value("schemaVersion").toInt() > 2)
                return fail(tr("不支持此工程版本"));
            if (manifest.value("engineRevision").toString() != EngineRevision)
                return fail(tr("此工程来自不同版本的乐谱内核"));
            for (const auto &v : manifest.value("importWarnings").toArray())
                newWarnings << v.toString();
            auto viewBytes = zip.fileData("view.json").toQByteArray();
            QJsonParseError jsonError;
            auto viewDoc = QJsonDocument::fromJson(viewBytes, &jsonError);
            if (jsonError.error != QJsonParseError::NoError || !viewDoc.isObject())
                return fail(tr("工程显示设置损坏"));
            newView = viewDoc.object();
            if (manifest.value("schemaVersion").toInt() == 2 && !zip.fileExists("input.json"))
                return fail(tr("工程缺少文字稿数据"));
            if (zip.fileExists("input.json"))
            {
                auto inputDoc =
                    QJsonDocument::fromJson(zip.fileData("input.json").toQByteArray(), &jsonError);
                if (jsonError.error != QJsonParseError::NoError || !inputDoc.isObject())
                    return fail(tr("工程文字稿损坏"));
                newInput = inputDoc.object();
            }
            auto bytes = zip.fileData("score.mscz");
            muse::io::Buffer buffer(&bytes);
            buffer.open(muse::io::IODevice::ReadOnly);
            MscReader::Params p;
            p.device = &buffer;
            p.filePath = "score.mscz";
            MscReader reader(p);
            auto ret = reader.open();
            if (!ret)
                return fail(retError(ret));
            ret = candidate->loadMscz(reader, nullptr, false);
            if (!ret)
                return fail(retError(ret));
        }
        else if (suffix == "xml" || suffix == "musicxml" || suffix == "mxl")
        {
            muse::ByteArray bytes;
            if (suffix == "mxl")
            {
                muse::ZipReader zip{muse::io::path_t(path)};
                QXmlStreamReader container(zip.fileData("META-INF/container.xml").toQByteArray());
                QString root;
                while (!container.atEnd())
                {
                    container.readNext();
                    if (container.isStartElement() && container.name() == u"rootfile")
                    {
                        root = container.attributes().value("full-path").toString();
                        break;
                    }
                }
                if (container.hasError() || root.isEmpty() || zip.hasError())
                    return fail(tr("MXL 容器描述损坏"));
                bytes = zip.fileData(root.toStdString());
            }
            else
            {
                QFile input(path);
                if (!input.open(QIODevice::ReadOnly))
                    return fail(input.errorString());
                bytes = muse::ByteArray::fromQByteArray(input.readAll());
            }
            if (bytes.empty())
                return fail(tr("MusicXML 内容为空"));
            // Same two-pass importer used by Studio, exposing its diagnostics to our UI.
            ScoreLoad load;
            using namespace mu::iex::musicxml;
            MusicXmlLogger logger;
            logger.setLoggingLevel(MusicXmlLogger::Level::MXML_ERROR);
            MusicXmlParserPass1 pass1(score, &logger);
            auto err = pass1.parse(bytes);
            MusicXmlParserPass2 pass2(score, pass1, &logger);
            if (err == Err::NoError)
                err = pass2.parse(bytes);
            QString details = (pass1.errors() + pass2.errors()).toQString();
            if (err != Err::NoError)
                return fail(tr("MusicXML 导入失败（%1）\n%2").arg(int(err)).arg(details));
            if (!details.isEmpty())
                newWarnings << details;
            for (const auto *part : score->parts())
                for (const auto &entry : part->instruments())
                    entry.second->updateInstrumentId();
        }
        else if (suffix == "mid" || suffix == "midi")
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return fail(file.errorString());
            const auto h = file.read(14);
            if (h.size() < 14 || h.left(4) != "MThd")
                return fail(tr("MIDI 文件头损坏"));
            auto u16 = [&h](int offset)
            { return (quint8(h[offset]) << 8) | quint8(h[offset + 1]); };
            if (u16(8) > 1 || (u16(12) & 0x8000) || u16(12) == 0)
                return fail(tr("首版支持 SMF 0／1 和 PPQ 时间制 MIDI"));
            using namespace mu::iex::midi;
            auto &data = midiImportOperations;
            if (data.hasMidiFile(path))
                data.excludeMidiFile(path);
            data.addNewMidiFile(path);
            NotationMidiReader reader;
            // Studio's first conversion is the track-discovery pass and deliberately
            // ignores doImport. Discover into a separate score, then apply options to
            // a fresh candidate so the actual document contains precisely those tracks.
            auto discovery = EngravingProject::create(context());
            discovery->setFileInfoProvider(
                std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
            auto ret = reader.read(discovery->masterScore(), muse::io::path_t(path), {});
            if (!ret)
            {
                data.excludeMidiFile(path);
                return fail(retError(ret));
            }
            {
                MidiOperations::CurrentMidiFileSetter current(data, path);
                auto &op = data.data()->trackOpers;
                int quant = options.quantization == 32 ? 3 : options.quantization == 64 ? 4 : 2;
                op.quantValue.setDefaultValue(static_cast<MidiOperations::QuantValue>(quant),
                                              false);
                op.searchTuplets.setDefaultValue(options.tuplets, false);
                for (int i = 0; i < data.data()->trackCount; ++i)
                    op.trackIndexAfterReorder.setValue(i, i);
                if (!options.tracks.isEmpty())
                {
                    op.doImport.setDefaultValue(false);
                    for (int track : options.tracks)
                    {
                        if (track < 0 || track >= data.data()->trackCount)
                        {
                            data.excludeMidiFile(path);
                            return fail(tr("MIDI 轨道选择无效"));
                        }
                        op.doImport.setValue(track, true);
                    }
                }
            }
            ret = reader.read(score, muse::io::path_t(path), {});
            data.excludeMidiFile(path);
            if (!ret)
                return fail(retError(ret));
        }
        else
            return fail(tr("不支持的文件格式：%1").arg(suffix));
        if (!score->firstMeasure())
            return fail(tr("曲谱中没有可用的小节"));
        auto ret = candidate->setupMasterScore(false);
        if (!ret)
            return fail(tr("曲谱结构检查失败：%1").arg(retError(ret)));
        if (!newInput.isEmpty())
        {
            if (newInput.value("format").toString("fanqie-1.0") != "fanqie-1.0")
                return fail(tr("不支持此文字稿格式"));
            const auto sourceSize = newInput.value("source").toString().size();
            for (auto value : newInput.value("bindings").toArray())
            {
                auto b = value.toObject();
                auto denominator = b.value("d").toInteger(0);
                auto numerator = b.value("n").toInteger(-1);
                int offset = b.value("offset").toInt(-1), length = b.value("length").toInt(0),
                    track = b.value("track").toInt(-1);
                if (denominator <= 0 || numerator < 0 || offset < 0 || length < 1 ||
                    qint64(offset) + length > sourceSize || track < 0 ||
                    track >= int(score->ntracks()))
                    return fail(tr("文字稿元素关联损坏"));
            }
        }
        score->rebuildMidiMapping();
        m_project = std::move(candidate);
        m_path = native ? path : QString();
        m_view = newView;
        m_input = newInput;
        m_history.clear();
        m_mergeKey.clear();
        m_savedView = newView;
        m_imported = !native;
        m_warnings = newWarnings;
        for (auto *staff : score->staves())
        {
            if (staff->isDrumStaff(Fraction(0, 1)))
                m_warnings << tr("打击乐数据已保留，暂不支持简谱编辑");
        }
        m_states = {0};
        m_cursor = 0;
        m_savedState = 0;
        syncSourceLayout();
        notify();
        emit loaded();
        return true;
    }
    catch (const std::exception &e)
    {
        return fail(QString::fromUtf8(e.what()));
    }
    catch (const QString &e)
    {
        return fail(e);
    }
}

bool ScoreDocument::save(const QString &path)
{
    m_error.clear();
    if (!m_project)
        return fail(tr("没有打开的曲谱"));
    auto native = muse::io::Buffer::opened(muse::io::IODevice::WriteOnly);
    MscWriter::Params p;
    p.device = &native;
    p.filePath = "score.mscz";
    MscWriter writer(p);
    auto ret = writer.open();
    if (!ret || !m_project->writeMscz(writer, false))
        return fail(tr("曲谱序列化失败"));
    writer.close();
    if (writer.hasError())
        return fail(tr("曲谱压缩失败"));
    auto container = muse::io::Buffer::opened(muse::io::IODevice::WriteOnly);
    muse::ZipWriter zip(&container);
    QJsonObject manifest{{"schemaVersion", 2},
                         {"engineRevision", EngineRevision},
                         {"application", "JianpuLab"},
                         {"importWarnings", QJsonArray::fromStringList(m_warnings)}};
    zip.addFile("manifest.json", muse::ByteArray::fromQByteArray(QJsonDocument(manifest).toJson()));
    zip.addFile("score.mscz", native.data());
    zip.addFile("view.json", muse::ByteArray::fromQByteArray(QJsonDocument(m_view).toJson()));
    zip.addFile("input.json", muse::ByteArray::fromQByteArray(QJsonDocument(m_input).toJson()));
    zip.close();
    if (zip.hasError())
        return fail(tr("工程打包失败"));
    if (!atomicWrite(path, container.data().toQByteArray(), m_error))
        return false;
    m_path = path;
    m_savedView = m_view;
    m_imported = false;
    m_savedState = m_states.at(m_cursor);
    emit changed();
    return true;
}

bool ScoreDocument::exportFile(const QString &path)
{
    m_error.clear();
    if (!m_project)
        return fail(tr("没有打开的曲谱"));
    if (m_input.value("hasErrors").toBool())
        return fail(tr("请先解决文字稿中的错误或待分配歌词，再导出。"));
    const auto suffix = QFileInfo(path).suffix().toLower();
    QTemporaryDir dir;
    if (!dir.isValid())
        return fail(tr("无法创建导出临时目录"));
    QString temp = dir.filePath("export." + suffix);
    auto *score = m_project->masterScore();
    score->rebuildMidiMapping();
    bool ok = false;
    if (suffix == "mid" || suffix == "midi")
        ok = mu::iex::midi::ExportMidi(score).write(temp, true, false);
    else if (suffix == "mxl")
        ok = mu::iex::musicxml::saveMxl(score, muse::String::fromQString(temp));
    else if (suffix == "xml" || suffix == "musicxml")
        ok = mu::iex::musicxml::saveXml(score, muse::String::fromQString(temp));
    else
        return fail(tr("不支持的导出格式"));
    if (!ok)
        return fail(tr("导出失败"));
    QFile file(temp);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    return atomicWrite(path, file.readAll(), m_error);
}

bool ScoreDocument::newScore()
{
    QTemporaryDir dir;
    QFile file(dir.filePath("new.musicxml"));
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    QByteArray xml =
        R"(<?xml version="1.0" encoding="utf-8"?><score-partwise version="4.0"><work><work-title>未命名曲谱</work-title></work><part-list><score-part id="P1"><part-name>旋律</part-name></score-part></part-list><part id="P1">)";
    for (int i = 1; i <= 1; ++i)
    {
        xml += "<measure number=\"" + QByteArray::number(i) + "\">";
        if (i == 1)
            xml += "<attributes><divisions>1</divisions><key><fifths>0</fifths></"
                   "key><time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</"
                   "sign><line>2</line></clef></attributes>";
        xml +=
            "<note><rest measure=\"yes\"/><duration>4</duration><voice>1</voice></note></measure>";
    }
    xml += "</part></score-partwise>";
    file.write(xml);
    file.close();
    if (!open(file.fileName()))
        return false;
    m_input = {{"format", "fanqie-1.0"},
               {"source", QStringLiteral("V: 1.0\nB: 未命名曲谱\nD: C\nP: 4/4\nQ: ")},
               {"bindings", QJsonArray()}};
    m_imported = true;
    notify();
    return true;
}

bool ScoreDocument::edit(const EditRequest &request)
{
    if (!m_project)
        return fail(tr("没有打开的曲谱"));
    const auto previousState = historyState();
    const auto previousSnapshot = snapshot();
    m_mergeKey.clear();
    auto *score = m_project->masterScore();
    std::vector<EngravingItem *> items;
    for (const auto &id : request.ids)
    {
        auto eid = EID::fromStdString(id.toStdString());
        auto *obj = score->eidRegister()->itemFromEID(eid);
        if (!obj || !obj->isEngravingItem())
            return fail(tr("选择已失效，请重新选择"));
        auto *item = static_cast<EngravingItem *>(obj);
        if (item->staff() && item->staff()->isDrumStaff(item->tick()))
            return fail(tr("暂不支持编辑打击乐声部"));
        items.push_back(item);
    }
    using K = EditRequest::Kind;
    if (items.empty() && request.kind != K::AppendMeasure && request.kind != K::SetKey &&
        request.kind != K::SetTime && request.kind != K::AddPart)
        return fail(tr("请先选择音符或休止符"));
    auto *first = items.empty() ? nullptr : items.front();
    auto crFor = [](EngravingItem *item) -> ChordRest *
    {
        if (!item)
            return nullptr;
        if (item->isNote())
            return toNote(item)->chord();
        if (item->isChordRest())
            return toChordRest(item);
        return nullptr;
    };
    auto *firstCR = crFor(first);
    bool ok = true;
    QString message;
    size_t before = score->undoStack()->currentIndex();
    score->deselectAll();
    if (!items.empty())
        score->select(items);
    score->startCmd(muse::TranslatableString::untranslatable("简谱编辑"));
    auto &tx = *score->transactionManager()->currentTransaction();
    auto reject = [&](const QString &error)
    {
        ok = false;
        message = error;
    };
    try
    {
        switch (request.kind)
        {
        case K::DiatonicUp:
        case K::DiatonicDown:
        case K::OctaveUp:
        case K::OctaveDown:
            EditNote::upDown(score, request.kind == K::DiatonicUp || request.kind == K::OctaveUp,
                             request.kind == K::OctaveUp || request.kind == K::OctaveDown
                                 ? UpDownMode::OCTAVE
                                 : UpDownMode::DIATONIC);
            break;
        case K::Sharp:
        case K::Flat:
        case K::Natural:
            for (auto *item : items)
                if (item->isNote())
                {
                    auto *note = toNote(item);
                    auto key = note->staff()->concertKey(note->tick());
                    int tpc = step2tpcByKey(tpc2step(note->tpc1()), key);
                    tpc += (request.kind == K::Sharp  ? 1
                            : request.kind == K::Flat ? -1
                                                      : 0) *
                           TPC_DELTA_SEMITONE;
                    int pitch = note->pitch() + int(tpc2alter(tpc)) - int(tpc2alter(note->tpc1()));
                    if (pitch < 0 || pitch > 127)
                    {
                        reject(tr("音高超出 MIDI 范围"));
                        break;
                    }
                    auto interval = note->staff()->transpose(note->tick());
                    interval.flip();
                    EditNote::undoChangePitch(score, note, pitch, tpc,
                                              Transpose::transposeTpc(tpc, interval, true));
                }
            break;
        case K::SetDegree:
        case K::InsertRest:
        {
            if (!firstCR || request.value < 0 || request.value > 7)
            {
                reject(tr("请选择一个有效输入位置"));
                break;
            }
            NoteVal val;
            if (request.kind == K::SetDegree && request.value > 0)
            {
                auto key = firstCR->staff()->concertKey(firstCR->tick());
                int tonic = (int(key) * 4 % 7 + 7) % 7;
                int absoluteDegree = tonic + request.value - 1;
                int step = absoluteDegree % 7;
                int tpc = step2tpcByKey(step, key);
                int octave = 4;
                if (first->isNote())
                {
                    auto *n = toNote(first);
                    int diatonic = playingOctave(n->pitch(), n->tpc1()) * 7 + tpc2step(n->tpc1());
                    octave = int(std::floor((diatonic - tonic) / 7.0));
                }
                octave += absoluteDegree / 7;
                val.pitch = std::clamp((octave + 1) * 12 + tpc2pitch(tpc), 0, 127);
                val.tpc1 = tpc;
                auto interval = firstCR->staff()->transpose(firstCR->tick());
                interval.flip();
                val.tpc2 = Transpose::transposeTpc(tpc, interval, true);
                if (first->isNote())
                {
                    EditNote::undoChangePitch(score, toNote(first), val.pitch, val.tpc1, val.tpc2);
                    break;
                }
            }
            TDuration inputDuration(Fraction(1, request.secondary > 0 ? request.secondary : 4));
            inputDuration.setDots(std::clamp(request.dots, 0, 2));
            Fraction length = request.secondary > 0 ? inputDuration.fraction() : firstCR->ticks();
            if (!score->setNoteRest(firstCR->segment(), firstCR->track(), val, length))
                reject(tr("无法在此处输入音符"));
            break;
        }
        case K::Duration:
        {
            if (request.value < 1 || request.value > 128 || (request.value & (request.value - 1)) ||
                request.secondary < 0 || request.secondary > 2)
            {
                reject(tr("不支持此时值"));
                break;
            }
            QSet<ChordRest *> seen;
            for (auto *item : items)
            {
                auto *cr = crFor(item);
                if (!cr || seen.contains(cr))
                    continue;
                seen.insert(cr);
            }
            // Edit backwards because extending a duration may consume following events.
            auto targets = seen.values();
            std::sort(targets.begin(), targets.end(),
                      [](auto *a, auto *b) { return a->tick() > b->tick(); });
            TDuration duration(Fraction(1, request.value));
            duration.setDots(request.secondary);
            for (auto *cr : targets)
                score->changeCRlen(cr, duration);
            break;
        }
        case K::Delete:
            score->cmdDeleteSelection();
            break;
        case K::RemoveMark:
            for (auto *item : items)
                if (!item->isNote() && !item->isChordRest() && !item->isMeasure())
                    score->undoRemoveElement(item);
            break;
        case K::AddChordNote:
        {
            if (!firstCR || !firstCR->isChord())
            {
                reject(tr("请选择一个和弦音符"));
                break;
            }
            auto *chord = toChord(firstCR);
            auto *source = first->isNote() ? toNote(first) : chord->upNote();
            int pitch = request.value > 0 ? request.value : std::min(127, source->pitch() + 4);
            for (auto *n : chord->notes())
                if (n->pitch() == pitch)
                    reject(tr("和弦中已有此音高"));
            if (!ok)
                break;
            auto *n = Factory::createNote(chord);
            n->setPitch(pitch);
            n->setTpcFromPitch();
            n->setTrack(chord->track());
            score->undoAddElement(n);
            break;
        }
        case K::Lyrics:
        {
            if (!firstCR || !firstCR->isChord() || request.value < 0 || request.value > 1023)
            {
                reject(tr("歌词需要附着在音符上，段数范围 1–1024"));
                break;
            }
            Lyrics *lyric = nullptr;
            for (auto *l : firstCR->lyrics())
                if (l->verse() == request.value)
                    lyric = l;
            if (lyric)
                lyric->undoChangeProperty(Pid::TEXT,
                                          muse::String::fromQString(request.text.toHtmlEscaped()));
            else if (!request.text.isEmpty())
            {
                lyric = Factory::createLyrics(firstCR);
                lyric->setTrack(firstCR->track());
                lyric->setVerse(request.value);
                lyric->setXmlText(muse::String::fromQString(request.text.toHtmlEscaped()));
                score->undoAddElement(lyric);
            }
            break;
        }
        case K::AppendMeasure:
            score->insertMeasure();
            break;
        case K::AddPart:
        {
            auto *instrument = searchTemplate(muse::String::fromQString(request.text));
            if (!instrument)
            {
                reject(tr("找不到乐器模板"));
                break;
            }
            EditPart::insertPart(score, instrument, score->parts().size());
            break;
        }
        case K::DeleteMeasure:
        {
            auto *measure = firstCR                       ? firstCR->measure()
                            : first && first->isMeasure() ? toMeasure(first)
                                                          : nullptr;
            if (!measure || score->firstMeasure() == score->lastMeasure())
            {
                reject(tr("至少保留一个小节"));
                break;
            }
            score->deleteMeasures(measure, measure);
            break;
        }
        case K::SetKey:
        {
            if (request.value < -7 || request.value > 7)
            {
                reject(tr("调号范围为七个降号到七个升号"));
                break;
            }
            auto tick = firstCR ? firstCR->measure()->tick() : Fraction(0, 1);
            KeySigEvent key;
            key.setConcertKey(Key(request.value));
            key.setMode(request.secondary ? KeyMode::MINOR : KeyMode::MAJOR);
            for (auto *staff : score->staves())
                if (!staff->isDrumStaff(tick))
                    EditKeySig::undoChangeKeySig(tx, score, staff, tick, key);
            break;
        }
        case K::SetTime:
        {
            if (request.value < 1 || request.value > 32 || request.secondary < 1 ||
                request.secondary > 32 || (request.secondary & (request.secondary - 1)))
            {
                reject(tr("不支持的拍号"));
                break;
            }
            auto *measure = firstCR ? firstCR->measure() : score->firstMeasure();
            auto *ts = Factory::createTimeSig(score->dummy());
            ts->setSig(Fraction(request.value, request.secondary));
            EditTimeSig::addTimeSig(tx, score, measure, 0, ts, false);
            break;
        }
        case K::AddVoice:
        {
            if (!firstCR)
            {
                reject(tr("请选择要添加声部的谱表"));
                break;
            }
            int baseTrack = int(firstCR->track() / VOICES) * VOICES;
            int target = -1;
            for (int v = 1; v < VOICES; ++v)
            {
                bool used = false;
                for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
                    if (m->firstChordRest(baseTrack + v))
                    {
                        used = true;
                        break;
                    }
                if (!used)
                {
                    target = baseTrack + v;
                    break;
                }
            }
            if (target < 0)
            {
                reject(tr("每个谱表最多四个声部"));
                break;
            }
            for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
            {
                auto *rest = Factory::createRest(score->dummy());
                rest->setTrack(target);
                rest->setDurationType(DurationType::V_MEASURE);
                rest->setTicks(m->ticks());
                score->undoAddCR(rest, m, m->tick());
            }
            break;
        }
        case K::Paste:
        {
            if (!firstCR)
            {
                reject(tr("请选择粘贴起点"));
                break;
            }
            auto json = QJsonDocument::fromJson(request.text.toUtf8()).object();
            QMimeData data;
            data.setData(json.value("format").toString(),
                         QByteArray::fromBase64(json.value("data").toString().toLatin1()));
            QMimeDataAdapter adapter(&data);
            if (!Paste::paste(tx, score, &adapter, nullptr))
                reject(tr("无法在此位置粘贴内容"));
            break;
        }
        case K::Grace:
            if (!firstCR || !firstCR->isChord())
                reject(tr("请选择倚音的主音"));
            else
                score->setGraceNote(toChord(firstCR),
                                    request.value > 0 ? request.value
                                                      : toChord(firstCR)->upNote()->pitch(),
                                    NoteType::ACCIACCATURA, 240);
            break;
        case K::AddMark:
        {
            if (!firstCR)
            {
                reject(tr("请选择记号起点"));
                break;
            }
            auto *segment = firstCR->segment();
            auto *endCR = crFor(items.back());
            auto track = firstCR->track();
            auto tick = firstCR->tick();
            QString kind = request.text;
            if (kind.startsWith("dynamic:"))
            {
                auto *mark = Factory::createDynamic(segment);
                mark->setTrack(track);
                mark->setDynamicType(muse::String::fromQString(kind.mid(8)));
                score->undoAddElement(mark);
            }
            else if (kind == "tempo")
            {
                if (request.value < 10 || request.value > 400)
                {
                    reject(tr("速度范围 10–400 BPM"));
                    break;
                }
                auto *mark = Factory::createTempoText(segment);
                mark->setTrack(track);
                mark->setTempo(BeatsPerSecond(request.value / 60.0));
                mark->setXmlText(
                    muse::String::fromQString(QStringLiteral("♩ = %1").arg(request.value)));
                score->undoAddElement(mark);
            }
            else if (kind == "repeat-start" || kind == "repeat-end")
            {
                auto *m = firstCR->measure();
                m->undoChangeProperty(kind == "repeat-start" ? Pid::REPEAT_START : Pid::REPEAT_END,
                                      kind == "repeat-start" ? !m->repeatStart() : !m->repeatEnd());
            }
            else if (kind.startsWith("jump:"))
            {
                auto *mark = Factory::createJump(firstCR->measure());
                mark->setTrack(track);
                static const QMap<QString, JumpType> types{{"DC", JumpType::DC},
                                                           {"DS", JumpType::DS},
                                                           {"DC_FINE", JumpType::DC_AL_FINE},
                                                           {"DS_FINE", JumpType::DS_AL_FINE},
                                                           {"DC_CODA", JumpType::DC_AL_CODA},
                                                           {"DS_CODA", JumpType::DS_AL_CODA}};
                mark->setJumpType(types.value(kind.mid(5), JumpType::DC));
                score->undoAddElement(mark);
            }
            else if (kind.startsWith("marker:"))
            {
                auto *mark = Factory::createMarker(firstCR->measure());
                mark->setTrack(track);
                static const QMap<QString, MarkerType> types{{"segno", MarkerType::SEGNO},
                                                             {"coda", MarkerType::CODA},
                                                             {"fine", MarkerType::FINE},
                                                             {"tocoda", MarkerType::TOCODA}};
                mark->setMarkerType(types.value(kind.mid(7), MarkerType::FINE));
                score->undoAddElement(mark);
            }
            else if (kind.startsWith("articulation:"))
            {
                if (!firstCR->isChord())
                {
                    reject(tr("此记号需要音符"));
                    break;
                }
                auto sym = SymNames::symIdByName(kind.mid(13).toStdString());
                if (sym == SymId::noSym)
                {
                    reject(tr("未知记号"));
                    break;
                }
                Articulation *mark =
                    kind.mid(13).startsWith("ornament")
                        ? static_cast<Articulation *>(Factory::createOrnament(firstCR))
                        : Factory::createArticulation(firstCR);
                mark->setTrack(track);
                mark->setSymId(sym);
                score->undoAddElement(mark);
            }
            else if (kind == "fermata")
            {
                auto *mark = Factory::createFermata(segment);
                mark->setTrack(track);
                mark->setSymIdAndTimeStretch(SymId::fermataAbove);
                score->undoAddElement(mark);
            }
            else if (kind == "tuplet")
            {
                if (!score->addTuplet(firstCR, Fraction(3, 2), TupletNumberType::SHOW_NUMBER,
                                      TupletBracketType::AUTO_BRACKET))
                    reject(tr("无法创建三连音"));
            }
            else if (kind == "slur" || kind == "tie" || kind == "crescendo" ||
                     kind == "diminuendo" || kind == "volta")
            {
                if (!endCR || endCR->tick() <= tick || endCR->track() != track)
                {
                    reject(tr("请选择同一声部内按时间先后排列的两个端点"));
                    break;
                }
                Spanner *spanner = nullptr;
                if (kind == "tie")
                {
                    if (!first->isNote() || !items.back()->isNote() ||
                        toNote(first)->pitch() != toNote(items.back())->pitch())
                    {
                        reject(tr("延音线需要两个同音高音符"));
                        break;
                    }
                    auto *tie = Factory::createTie(first);
                    tie->setStartNote(toNote(first));
                    tie->setEndNote(toNote(items.back()));
                    spanner = tie;
                }
                else if (kind == "slur")
                {
                    auto *slur = Factory::createSlur(score->dummy());
                    slur->setStartElement(firstCR);
                    slur->setEndElement(endCR);
                    spanner = slur;
                }
                else if (kind == "volta")
                {
                    auto *volta = Factory::createVolta(score->dummy());
                    volta->setEndings({std::max(1, request.value)});
                    volta->setText(muse::String::fromQString(
                        QString::number(std::max(1, request.value)) + "."));
                    spanner = volta;
                }
                else
                {
                    auto *hp = Factory::createHairpin(score->dummy());
                    hp->setHairpinType(kind == "crescendo" ? HairpinType::CRESC_HAIRPIN
                                                           : HairpinType::DIM_HAIRPIN);
                    spanner = hp;
                }
                spanner->setTrack(track);
                spanner->setTrack2(track);
                spanner->setTick(tick);
                spanner->setTick2(kind == "volta" ? endCR->measure()->endTick() : endCR->tick());
                score->undoAddElement(spanner);
            }
            else
                reject(tr("不支持的记号类型"));
            break;
        }
        case K::UpdateMark:
        {
            if (first->isDynamic())
            {
                auto type = TConv::fromXml(muse::AsciiStringView(request.text.toStdString()),
                                           DynamicType::OTHER);
                if (type == DynamicType::OTHER)
                {
                    reject(tr("不支持的力度"));
                    break;
                }
                first->undoChangeProperty(Pid::DYNAMIC_TYPE, type);
                first->undoChangeProperty(Pid::TEXT, Dynamic::dynamicText(type));
            }
            else if (first->isTempoText())
            {
                if (request.value < 10 || request.value > 400)
                {
                    reject(tr("速度范围 10–400 BPM"));
                    break;
                }
                first->undoChangeProperty(Pid::TEMPO, BeatsPerSecond(request.value / 60.0));
                first->undoChangeProperty(
                    Pid::TEXT,
                    muse::String::fromQString(QStringLiteral("♩ = %1").arg(request.value)));
            }
            else if (first->isHairpin())
                first->undoChangeProperty(
                    Pid::HAIRPIN_TYPE,
                    int(request.value ? HairpinType::DIM_HAIRPIN : HairpinType::CRESC_HAIRPIN));
            else if (first->isVolta())
            {
                first->undoChangeProperty(
                    Pid::VOLTA_ENDING,
                    PropertyValue::fromValue(std::vector<int>{std::clamp(request.value, 1, 16)}));
                first->undoChangeProperty(
                    Pid::BEGIN_TEXT, muse::String::fromQString(
                                         QString::number(std::clamp(request.value, 1, 16)) + "."));
            }
            else
                reject(tr("此记号可通过删除并重新选择端点修改"));
            break;
        }
        }
        if (ok)
        {
            auto ret = score->sanityCheck();
            if (!ret)
                reject(tr("修改后曲谱结构检查失败：%1").arg(retError(ret)));
        }
    }
    catch (const std::exception &e)
    {
        reject(QString::fromUtf8(e.what()));
    }
    catch (const QString &e)
    {
        reject(e);
    }
    score->endCmd(!ok);
    if (!ok)
        return fail(message);
    if (score->undoStack()->currentIndex() != before)
    {
        syncSourceAfterEdit(request, previousSnapshot);
        syncSourceLayout();
        recordChange(previousState, true);
    }
    notify();
    return true;
}

QByteArray ScoreDocument::copy(const QStringList &ids) const
{
    if (!m_project || ids.isEmpty())
        return {};
    auto *score = m_project->masterScore();
    std::vector<EngravingItem *> items;
    for (const auto &id : ids)
    {
        auto *obj = score->eidRegister()->itemFromEID(EID::fromStdString(id.toStdString()));
        if (obj && obj->isEngravingItem())
            items.push_back(static_cast<EngravingItem *>(obj));
    }
    if (items.empty())
        return {};
    const auto previousFilter = score->selectionFilter();
    score->selectionFilter().setFiltered(VoicesSelectionFilterTypes::ALL, false);
    for (auto *item : items)
        score->selectionFilter().setFiltered(
            static_cast<VoicesSelectionFilterTypes>(1u << (item->track() % VOICES)), true);
    score->deselectAll();
    if (items.size() == 1)
        score->select(items.front());
    else
    {
        auto crFor = [](EngravingItem *e) -> ChordRest *
        {
            return e->isNote() ? toNote(e)->chord() : e->isChordRest() ? toChordRest(e) : nullptr;
        };
        ChordRest *first = nullptr;
        ChordRest *last = nullptr;
        size_t low = muse::nidx, high = 0;
        for (auto *item : items)
            if (auto *cr = crFor(item))
            {
                if (!first || cr->tick() < first->tick())
                    first = cr;
                if (!last || cr->tick() + cr->actualTicks() > last->tick() + last->actualTicks())
                    last = cr;
                low = std::min(low, cr->staffIdx());
                high = std::max(high, cr->staffIdx());
            }
        if (!first || !last)
        {
            score->selectionFilter() = previousFilter;
            return {};
        }
        score->selection().setRangeTicks(first->tick(), last->tick() + last->actualTicks(), low,
                                         high + 1);
        score->selection().updateSelectedElements();
        score->selection().update();
    }
    auto data = score->selection().mimeData().toQByteArray();
    score->selectionFilter() = previousFilter;
    QJsonObject json{{"format", score->selection().mimeType().toQString()},
                     {"data", QString::fromLatin1(data.toBase64())}};
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

ScoreSnapshot ScoreDocument::snapshot() const
{
    ScoreSnapshot result;
    result.revision = m_revision;
    result.warnings = m_warnings;
    if (!m_project)
        return result;
    auto *score = m_project->masterScore();
    auto subtitles =
        QJsonDocument::fromJson(score->metaTag(u"jianpuSubtitles").toQString().toUtf8()).array();
    for (auto v : subtitles)
        result.subtitles << v.toString();
    auto lyricRows = m_input.value("verseRows").toObject();
    for (auto it = lyricRows.begin(); it != lyricRows.end(); ++it)
        result.lyricRows[it.key().toInt()] = it.value().toInt();
    auto authors =
        QJsonDocument::fromJson(score->metaTag(u"jianpuAuthors").toQString().toUtf8()).array();
    for (auto v : authors)
        result.authors << v.toString();
    if (result.authors.isEmpty())
    {
        auto composer = score->metaTag(u"composer").toQString();
        auto lyricist = score->metaTag(u"lyricist").toQString();
        if (!lyricist.isEmpty())
            result.authors << lyricist + QStringLiteral(" 词");
        if (!composer.isEmpty())
            result.authors << composer + QStringLiteral(" 曲");
    }
    result.title = score->metaTag(u"workTitle").toQString();
    if (result.title.isEmpty())
        result.title = tr("未命名曲谱");
    if (!score->staves().empty())
    {
        result.keyFifths = int(score->staff(0)->concertKey(Fraction(0, 1)));
        result.minor = score->staff(0)->keySigEvent(Fraction(0, 1)).mode() == KeyMode::MINOR;
    }
    QSet<int> usedTracks;
    QSet<QString> tuplets;
    for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
    {
        MeasureData measure;
        measure.id = idFor(m);
        measure.number = m->measureNumber() + 1;
        measure.tick = m->tick().ticks();
        measure.duration = m->ticks().ticks();
        measure.numerator = m->timesig().numerator();
        measure.denominator = m->timesig().denominator();
        measure.repeatStart = m->repeatStart();
        measure.repeatEnd = m->repeatEnd();
        measure.barline = int(m->endBarLineType());
        for (auto *segment = m->first(); segment; segment = segment->next())
        {
            if (!segment->isChordRestType())
                continue;
            for (size_t track = 0; track < score->ntracks(); ++track)
            {
                auto *item = segment->element(track);
                if (!item || (!item->isChord() && !item->isRest()))
                    continue;
                auto *cr = toChordRest(item);
                EventData event;
                event.id = idFor(cr);
                event.track = int(track);
                event.keyFifths = int(cr->staff()->concertKey(cr->tick()));
                event.tick = cr->tick().ticks();
                event.duration = cr->actualTicks().ticks();
                event.denominator =
                    cr->durationType().isMeasure()
                        ? 0
                        : TDuration(cr->durationType().type()).fraction().denominator();
                event.position = {cr->tick().numerator(), cr->tick().denominator()};
                event.length = {cr->actualTicks().numerator(), cr->actualTicks().denominator()};
                event.visible = cr->visible();
                if (cr->beam() &&
                    std::any_of(cr->beam()->elements().begin(), cr->beam()->elements().end(),
                                [](const auto *member)
                                {
                                    return member->beamMode() != BeamMode::AUTO &&
                                           member->beamMode() != BeamMode::INVALID;
                                }))
                    event.beamGroup = idFor(cr->beam());
                event.dots = cr->dots();
                event.rest = cr->isRest();
                event.tuplet = cr->tuplet() ? cr->tuplet()->ratio().numerator() : 0;
                event.tupletNormal = cr->tuplet() ? cr->tuplet()->ratio().denominator() : 0;
                event.tupletGroup = cr->tuplet() ? idFor(cr->tuplet()) : QString();
                if (auto *t = cr->tuplet())
                {
                    auto id = idFor(t);
                    if (!tuplets.contains(id))
                    {
                        tuplets.insert(id);
                        auto firstTick = cr->tick();
                        auto lastTick = cr->tick() + cr->actualTicks();
                        std::function<void(DurationElement *)> include =
                            [&](DurationElement *member)
                        {
                            if (member->isChordRest())
                            {
                                firstTick = std::min(firstTick, member->tick());
                                lastTick =
                                    std::max(lastTick, member->tick() + member->actualTicks());
                            }
                            else if (member->isTuplet())
                                for (auto *child : toTuplet(member)->elements())
                                    include(child);
                        };
                        for (auto *member : t->elements())
                            include(member);
                        result.marks.push_back({id, "tuplet",
                                                QString::number(t->ratio().numerator()), int(track),
                                                firstTick.ticks(), lastTick.ticks()});
                    }
                }
                usedTracks.insert(int(track));
                if (item->isChord())
                {
                    for (auto *art : toChord(item)->articulations())
                    {
                        auto name = art->articulationName().toQString();
                        if (art->symId() == SymId::ornamentTrill)
                            name = "trill";
                        else if (art->symId() == SymId::ornamentTurn)
                            name = "turn";
                        else if (name == "---")
                            name = art->subtypeUserName().translated().toQString();
                        static const QMap<QString, QString> names{
                            {"staccato", "·"}, {"tenuto", "—"},  {"sforzato", ">"}, {"accent", ">"},
                            {"trill", "tr"},   {"mordent", "~"}, {"turn", "∽"}};
                        event.articulations << names.value(name, name);
                        result.marks.push_back({idFor(art), "articulation", names.value(name, name),
                                                int(track), cr->tick().ticks()});
                    }
                    for (auto *note : toChord(item)->notes())
                    {
                        NoteData n;
                        n.id = idFor(note);
                        n.midi = note->pitch();
                        n.step = tpc2step(note->tpc1());
                        n.alter = int(tpc2alter(note->tpc1()));
                        n.octave = (n.midi - n.alter) / 12 - 1;
                        if (note->headGroup() == NoteHeadGroup::HEAD_CROSS)
                            event.rhythm = true;
                        n.tieStart = note->tieFor();
                        n.tieEnd = note->tieBack();
                        event.notes.push_back(n);
                        if (auto *tie = note->tieFor())
                        {
                            if (tie->endNote())
                                result.marks.push_back({idFor(tie),
                                                        "tie",
                                                        {},
                                                        int(track),
                                                        note->tick().ticks(),
                                                        tie->endNote()->tick().ticks(),
                                                        n.id,
                                                        idFor(tie->endNote())});
                        }
                    }
                }
                for (auto *lyric : cr->lyrics())
                    event.lyrics.push_back({lyric->verse(), lyric->plainText().toQString(),
                                            lyric->ticks().isNotZero()});
                measure.events.push_back(event);
                if (item->isChord())
                    for (auto *grace : toChord(item)->graceNotes())
                    {
                        EventData g;
                        g.id = idFor(grace);
                        g.track = int(track);
                        g.keyFifths = event.keyFifths;
                        g.tick = event.tick;
                        g.grace = true;
                        g.position = event.position;
                        g.graceAfter = grace->noteType() == NoteType::GRACE8_AFTER ||
                                       grace->noteType() == NoteType::GRACE16_AFTER ||
                                       grace->noteType() == NoteType::GRACE32_AFTER;
                        g.denominator =
                            TDuration(grace->durationType().type()).fraction().denominator();
                        if (grace->beam())
                            g.beamGroup = idFor(grace->beam());
                        for (auto *note : grace->notes())
                        {
                            NoteData n;
                            n.id = idFor(note);
                            n.midi = note->pitch();
                            n.step = tpc2step(note->tpc1());
                            n.alter = int(tpc2alter(note->tpc1()));
                            n.octave = playingOctave(n.midi, note->tpc1());
                            g.notes.push_back(n);
                        }
                        measure.events.push_back(g);
                    }
            }
        }
        result.measures.push_back(measure);
    }
    auto sorted = usedTracks.values();
    std::sort(sorted.begin(), sorted.end());
    for (int track : sorted)
    {
        auto *staff = score->staff(size_t(track / 4));
        int count = 0;
        for (int other : sorted)
            if (other / 4 == track / 4)
                ++count;
        auto name = staff->part()->partName().toQString();
        if (count > 1)
            name = tr("%1 · 声部 %2").arg(name).arg(track % 4 + 1);
        result.voices.push_back({track, name, staff->isDrumStaff(Fraction(0, 1))});
    }
    QSet<QString> known;
    score->scanElements(
        [&](EngravingItem *item)
        {
            QString kind, text;
            if (item->isDynamic())
            {
                kind = "dynamic";
                text = QString::fromLatin1(TConv::toXml(toDynamic(item)->dynamicType()).ascii());
            }
            else if (item->isTempoText())
            {
                kind = "tempo";
                text = toTempoText(item)->plainText().toQString();
            }
            else if (item->isMarker())
            {
                kind = "marker";
                text = TConv::translatedUserName(toMarker(item)->markerType()).toQString();
            }
            else if (item->isJump())
            {
                kind = "jump";
                text = TConv::translatedUserName(toJump(item)->jumpType()).toQString();
            }
            else if (item->isStaffText())
            {
                kind = "text";
                text = toStaffText(item)->plainText().toQString();
            }
            else if (item->isFermata())
            {
                kind = "fermata";
                text = QStringLiteral("𝄐");
            }
            else if (item->isKeySig() && !item->generated() && !toKeySig(item)->isCourtesy() &&
                     item->tick().isNotZero())
            {
                kind = toKeySig(item)->keySigEvent().mode() == KeyMode::MINOR ? "minor-key" : "key";
                text = QString::number(int(toKeySig(item)->keySigEvent().concertKey()));
            }
            else if (item->isTimeSig() && !item->generated() && !toTimeSig(item)->isCourtesy() &&
                     item->tick().isNotZero())
            {
                kind = "time";
                auto sig = toTimeSig(item)->sig();
                text = QStringLiteral("%1/%2").arg(sig.numerator()).arg(sig.denominator());
            }
            if (kind.isEmpty())
                return;
            auto id = idFor(item);
            if (!known.contains(id))
            {
                known.insert(id);
                result.marks.push_back({id, kind, text, int(item->track()), item->tick().ticks()});
            }
        });
    for (const auto &[tick, spanner] : score->spanner())
    {
        Q_UNUSED(tick);
        if (spanner->isTie())
            continue;
        QString kind, text;
        if (spanner->isSlur())
            kind = "slur";
        else if (spanner->isHairpin())
        {
            kind = "hairpin";
            text = toHairpin(spanner)->isCrescendo() ? "<" : ">";
        }
        else if (spanner->isVolta())
        {
            kind = "volta";
            text = toVolta(spanner)->text().toQString();
        }
        else
        {
            result.warnings << tr("保留了暂不支持简谱显示的跨音符记号：%1")
                                   .arg(QString::fromLatin1(spanner->typeName()));
            continue;
        }
        result.marks.push_back(
            {idFor(spanner), kind, text, int(spanner->track()), spanner->tick().ticks(),
             spanner->tick2().ticks(),
             spanner->startElement() ? idFor(spanner->startElement()) : QString(),
             spanner->endElement() ? idFor(spanner->endElement()) : QString()});
    }
    result.warnings.removeDuplicates();
    QMap<int, QMap<MusicalTime, EventData *>> eventsByPosition;
    for (auto &measure : result.measures)
        for (auto &event : measure.events)
            if (!event.grace)
                eventsByPosition[event.track][event.position] = &event;
    for (auto binding : m_input.value("bindings").toArray())
    {
        auto o = binding.toObject();
        auto denominator = o.value("d").toInteger(0);
        if (denominator <= 0)
            continue;
        MusicalTime position(o.value("n").toInteger(), denominator);
        if (auto *event = eventsByPosition[o["track"].toInt()].value(position, nullptr))
        {
            event->logicalId = o["id"].toString();
            event->sourceStart = o["offset"].toInt();
            event->sourceLength = o["length"].toInt();
            event->sourceBreak = o["break"].toBool();
        }
    }
    return result;
}
bool ScoreDocument::canUndo() const { return m_project && m_cursor > 0; }
bool ScoreDocument::canRedo() const { return m_project && m_cursor < m_history.size(); }
bool ScoreDocument::modified() const
{
    return m_project &&
           (m_imported || m_view != m_savedView || m_states.at(m_cursor) != m_savedState);
}
void ScoreDocument::setViewSettings(const QJsonObject &value, const QString &mergeKey)
{
    if (m_view == value)
        return;
    auto previous = historyState();
    m_view = value;
    syncSourceLayout();
    recordChange(previous, false, mergeKey);
    notify();
}
void ScoreDocument::recordChange(const HistoryState &before, bool engine, const QString &key)
{
    bool merge = !key.isEmpty() && key == m_mergeKey && m_cursor > 0 &&
                 m_cursor == m_history.size() && m_history.last().mergeKey == key && !engine &&
                 !m_history.last().engine && m_states[m_cursor] != m_savedState;
    if (merge)
    {
        m_history.last().after = historyState();
        m_states[m_cursor] = m_nextState++;
    }
    else
    {
        m_history.resize(m_cursor);
        m_states.resize(m_cursor + 1);
        m_history.push_back({before, historyState(), engine, key});
        m_states << m_nextState++;
        ++m_cursor;
    }
    m_mergeKey = key;
    if (m_history.size() > 128)
    {
        int remove = m_history.size() - 128;
        m_history.remove(0, remove);
        m_states.remove(0, remove);
        m_cursor -= remove;
    }
}
void ScoreDocument::undo()
{
    if (!canUndo())
        return;
    const auto &entry = m_history[m_cursor - 1];
    if (entry.engine)
        entry.after.project->masterScore()->undoRedo(true, nullptr);
    m_project = entry.before.project;
    m_view = entry.before.view;
    m_input = entry.before.input;
    --m_cursor;
    m_mergeKey.clear();
    notify();
}
void ScoreDocument::redo()
{
    if (!canRedo())
        return;
    const auto &entry = m_history[m_cursor];
    if (entry.engine)
        entry.before.project->masterScore()->undoRedo(false, nullptr);
    m_project = entry.after.project;
    m_view = entry.after.view;
    m_input = entry.after.input;
    ++m_cursor;
    m_mergeKey.clear();
    notify();
}
} // namespace jianpu
