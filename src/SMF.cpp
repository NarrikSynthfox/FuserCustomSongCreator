#include <array>
#include <sstream>
#include <map>

#include "stream-helpers.h"
#include "SMF.h"

constexpr int MThd = 0x4D546864;
constexpr int MTrk = 0x4D54726B;
constexpr int HEADER_SIZE = 6;

TrackEvent ReadEvent(std::istream& stream, uint8_t& running_status) {
    auto deltaTime = read_mb(stream);
    auto status = (uint8_t)stream.peek();
    // set this to true if we expected to use running status but a status byte was provided anyway.
    bool f_status = false;
    if (status < 0x80) // running status
    {
        status = running_status;
    }
    else
    {
        stream.get();
        f_status = (running_status == status);
        if (status < 0xF0) // meta events do not trigger running status?
            running_status = status;
    }
    uint8_t channel = status & 0xF;
    uint8_t key, velocity, pressure, controller, value;
    uint16_t bend;
    EventType eventType = (EventType)(status & 0xF0);
    if (eventType == EventType::NoteOff || eventType == EventType::NoteOn) {
        key = read_be<uint8_t>(stream);
        velocity = read_be<uint8_t>(stream);
        return TrackEvent{ deltaTime, eventType, MidiEvent{f_status, channel, {key, velocity}} };
    }
    else if (eventType == EventType::NotePresure) {
        key = read_be<uint8_t>(stream);
        pressure = read_be<uint8_t>(stream);
        return TrackEvent{ deltaTime, eventType, MidiEvent{f_status, channel, {key, pressure}} };
    }
    else if (eventType == EventType::Controller) {
        controller = read_be<uint8_t>(stream);
        value = read_be<uint8_t>(stream);
        return TrackEvent{ deltaTime, eventType, MidiEvent{f_status, channel, {controller, value}} };
    }
    else if (eventType == EventType::PitchBend) {
        MidiEvent bendEvent;
        bendEvent.force_status = f_status;
        bendEvent.channel = channel;
        bend = read_be<uint16_t>(stream);
        bendEvent.bend = bend;
        // I think this should have worked without designated initializers. But MSVC complains!
        return TrackEvent{ deltaTime, eventType, bendEvent };
    }
    else if (eventType == EventType::ProgramChange) {
        value = read_be<uint8_t>(stream);
        return TrackEvent{ deltaTime, eventType, MidiEvent{f_status, channel, value} };
    }
    else if (eventType == EventType::ChannelPressure) {
        pressure = read_be<uint8_t>(stream);
        return TrackEvent{ deltaTime, eventType, MidiEvent{f_status, channel, pressure} };
    }
    if (status == 0xFF) // meta event
    {
        auto type = read_be<MetaEventType>(stream);
        auto length = read_mb(stream);
        std::vector<uint8_t> tmp;
        switch (type)
        {
        case MetaEventType::SequenceNumber:
            if (length != 2)
                throw std::exception("Sequence number events must have 2 bytes of data");
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, read_be<uint16_t>(stream)) };
        case MetaEventType::Text:
        case MetaEventType::CopyrightNotice:
        case MetaEventType::TrackName:
        case MetaEventType::InstrumentName:
        case MetaEventType::Lyric:
        case MetaEventType::Marker:
        case MetaEventType::CuePoint:
        case MetaEventType::ProgramName:
        case MetaEventType::DeviceName:
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, read_str(stream, length)) };
        case MetaEventType::ChannelPrefix:
        case MetaEventType::Port:
            if (length != 1)
                throw std::exception("Channel prefix and port events must have 1 byte of data");
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, read_be<uint8_t>(stream)) };
        case MetaEventType::EndOfTrack:
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type) };
        case MetaEventType::TempoEvent:
            if (length != 3)
                throw std::exception("Tempo events must have 3 bytes of data");
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, read_i24_be(stream)) };
        case MetaEventType::SmpteOffset:
            if (length != 5)
                throw std::exception("SMTPE Offset events must have 5 bytes of data");
            tmp.resize(5);
            stream.read((char*)tmp.data(), 5);
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, SmpteOffsetEvent{tmp[0], tmp[1], tmp[2], tmp[3], tmp[4]}) };
        case MetaEventType::TimeSignature:
            if (length != 4)
                throw std::exception("Time Signature events must have 4 bytes of data");
            tmp.resize(4);
            stream.read((char*)tmp.data(), 4);
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, TimeSignatureEvent{tmp[0], tmp[1], tmp[2], tmp[3]}) };
        case MetaEventType::KeySignature:
            if (length != 2)
                throw std::exception("Key Signature events must have 2 bytes of data");
            tmp.resize(2);
            stream.read((char*)tmp.data(), 2);
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, KeySignatureEvent{tmp[0], tmp[1]}) };
        case MetaEventType::SequencerSpecific:
            tmp.resize(length);
            stream.read((char*)tmp.data(), length);
            return TrackEvent{ deltaTime, EventType::Meta, MetaEvent(type, tmp) };
        default: { // unknown meta event, just skip past it.
            std::ostringstream ss;
            ss << "Unknown meta event type " << std::hex << (int)type << " at 0x" << stream.tellg();
            throw std::exception(ss.str().c_str());
        } break;
        }
    }
    else // sysex
    {
        auto length = read_mb(stream);
        std::vector<uint8_t> data;
        if (status == 0xF0) // should prefix Sysex with F0 (start-of-exclusive)
        {
            data.resize(length + 1);
            data[0] = 0xF0;
            stream.read((char*)&data[1], length);
        }
        else
        {
            data.resize(length);
            stream.read((char*)data.data(), length);
        }
        return TrackEvent{ deltaTime, EventType::Sysex, SysexEvent{data} };
    }
}

MidiTrack ReadTrack(std::istream& stream) {
    if (read_be<int>(stream) != MTrk)
        throw std::exception("MIDI track not recognized.");
    int64_t track_length = read_be<uint32_t>(stream);
    int64_t total_ticks = 0;
    std::string name;
    std::vector<TrackEvent> events;
    uint8_t running_status = 0;
    int curEventIDX = 0;
    while (track_length > 0)
    {
        auto pos = stream.tellg();
        auto event = ReadEvent(stream, running_status);
        if (event.type == EventType::Meta && std::get<MetaEvent>(event.inner_event).type == MetaEventType::TrackName)
            name = std::get<std::string>(std::get<MetaEvent>(event.inner_event).event);
        total_ticks += event.delta_time;
        events.push_back(std::move(event));
        track_length -= stream.tellg() - pos; // subtract message length from total track length
        curEventIDX += 1;
    }
    return MidiTrack{ name, total_ticks, events };
}

MidiFile MidiFile::ReadMidi(std::istream& stream) {
    // "MThd" big-endian, header size always = 6
    if (read_be<int>(stream) != MThd || read_be<int>(stream) != HEADER_SIZE)
        throw std::exception("MIDI file did not begin with proper MIDI header.");
    MidiFormat format = read_be<MidiFormat>(stream);
    if (format > MidiFormat::MultiTrack) {
        std::ostringstream ss;
        ss << "MIDI format " << (int)format << " is not supported by this library.";
        throw std::exception(ss.str().c_str());
    }
    auto num_tracks = read_be<uint16_t>(stream);
    uint16_t ticks_per_qn = read_be<uint16_t>(stream);
    if ((ticks_per_qn & 0x8000) == 0x8000)
        throw std::exception("SMPTE delta time format is not supported by this library.");

    std::vector<MidiTrack> tracks;
    for (int i = 0; i < num_tracks; i++)
    {
        tracks.push_back(ReadTrack(stream));
    }
    return MidiFile(format, tracks, ticks_per_qn);
}

void WriteEvent(std::ostream& stream, const TrackEvent& event, uint8_t& running_status) {
    write_mb(stream, event.delta_time);
    if (event.type >= EventType::NoteOff && event.type <= EventType::PitchBend) {
        const auto& midi_event = std::get<MidiEvent>(event.inner_event);
        uint8_t status = midi_event.channel | (uint8_t)event.type;
        if (status != running_status || midi_event.force_status) {
            stream.put((char)status);
            running_status = status;
        }
        switch (event.type) {
        case EventType::NoteOff:
        case EventType::NoteOn:
            stream.put((char)midi_event.note.key);
            stream.put((char)midi_event.note.velocity);
            break;
        case EventType::NotePresure:
            stream.put((char)midi_event.note.key);
            stream.put((char)midi_event.note.pressure);
            break;
        case EventType::Controller:
            stream.put((char)midi_event.controller.controller);
            stream.put((char)midi_event.controller.value);
            break;
        case EventType::ProgramChange:
            stream.put((char)midi_event.program);
            break;
        case EventType::ChannelPressure:
            stream.put((char)midi_event.pressure);
            break;
        case EventType::PitchBend:
            write_be(stream, midi_event.bend);
            break;
        }
    }
    else if (event.type == EventType::Meta) {
        // cancel running status
        //running_status = 0;
        const auto& meta_event = std::get<MetaEvent>(event.inner_event);
        stream.put((char)0xFF);
        stream.put((char)meta_event.type);
        switch (meta_event.type)
        {
        case MetaEventType::SequenceNumber:
            write_mb(stream, 2);
            write_be(stream, std::get<uint16_t>(meta_event.event));
            break;
        case MetaEventType::Text:
        case MetaEventType::CopyrightNotice:
        case MetaEventType::TrackName:
        case MetaEventType::InstrumentName:
        case MetaEventType::Lyric:
        case MetaEventType::Marker:
        case MetaEventType::CuePoint:
        case MetaEventType::ProgramName:
        case MetaEventType::DeviceName: {
            const auto& text = std::get<std::string>(meta_event.event);
            write_mb(stream, text.size());
            stream.write(text.c_str(), text.size());
        } break;
        case MetaEventType::ChannelPrefix:
        case MetaEventType::Port:
            write_mb(stream, 1);
            stream.put((char)std::get<uint8_t>(meta_event.event));
            break;
        case MetaEventType::EndOfTrack:
            write_mb(stream, 0);
            break;
        case MetaEventType::TempoEvent:
            write_mb(stream, 3);
            write_i24_be(stream, std::get<uint32_t>(meta_event.event));
            break;
        case MetaEventType::SmpteOffset: {
            write_mb(stream, 5);
            const auto& x = std::get<SmpteOffsetEvent>(meta_event.event);
            std::array<uint8_t, 5> data{ x.h, x.m, x.s, x.f, x.frame_hundredths };
            stream.write((char*)data.data(), 5);
        } break;
        case MetaEventType::TimeSignature: {
            write_mb(stream, 4);
            const auto& x = std::get<TimeSignatureEvent>(meta_event.event);
            std::array<uint8_t, 4> data{ x.numerator, x.denominator, x.clocks_per_tick, x.thirtysecond_notes_per_24_clocks };
            stream.write((char*)data.data(), 4);
        } break;
        case MetaEventType::KeySignature: {
            write_mb(stream, 2);
            const auto& x = std::get<KeySignatureEvent>(meta_event.event);
            stream.put((char)x.sharps);
            stream.put((char)x.tonality);
        } break;
        case MetaEventType::SequencerSpecific: {
            const auto& x = std::get<std::vector<uint8_t>>(meta_event.event);
            write_mb(stream, x.size());
            stream.write((char*)x.data(), x.size());
        } break;
        }
    }
    else {
        // cancel running status
        //running_status = 0;
        const auto& sysex = std::get<SysexEvent>(event.inner_event);
        stream.put((char)0xF0);
        write_mb(stream, sysex.data.size());
        stream.write((char*)sysex.data.data(), sysex.data.size());
    }
}

void MidiFile::WriteMidi(std::ostream& stream) {
    write_be(stream, MThd);
    write_be(stream, HEADER_SIZE);
    write_be<uint16_t>(stream, (uint16_t)format_);
    write_be<uint16_t>(stream, (uint16_t)tracks_.size());
    write_be<uint16_t>(stream, ticks_per_qn_);
    for (const auto& t : tracks_) {
        write_be(stream, MTrk);
        std::ostringstream os;
        uint8_t running_status = 0;
        for (const auto& e : t.events) {
            WriteEvent(os, e, running_status);
        }
        auto track_data = os.str();
        write_be<uint32_t>(stream, track_data.size());
        stream.write(track_data.data(), track_data.size());
    }
}


const MidiTrack* MidiFile::GetTrackByName(std::string& name) const {
    for (const auto& track : tracks_) {
        if (track.name == name) {
            return &track;
        }
    }
    return nullptr;
}

const double MICROSECONDS_PER_SECOND = 1000000.0;
inline double TicksToSeconds(uint16_t ticks_per_qn, int64_t ticks, uint32_t micros_per_qn) {
    return ((double)ticks / ticks_per_qn) * (micros_per_qn / MICROSECONDS_PER_SECOND);
}
inline double TempoToBpm(uint32_t micros_per_qn) {
    return 60.0 / (micros_per_qn / MICROSECONDS_PER_SECOND);
}
double MidiFile::ProcessTempoMap() {
    double duration = 0;
    int64_t ticks = 0; // running total of MIDI ticks
    uint32_t micros_per_qn = 500000; // Current tempo. Defaults to 500,000 microseconds per QN

    // tempo+timesig map stuff
    auto tempos = std::map<int64_t, uint32_t>();
    auto sigs = std::map<int64_t, TimeSignatureEvent>();
    auto durations = std::map<int64_t, double>();

    for (const auto& m : tracks_[0].events) // tempo map track
    {
        ticks += m.delta_time;
        duration += TicksToSeconds(ticks_per_qn_, m.delta_time, micros_per_qn);
        if (m.type != EventType::Meta) continue;
        const MetaEvent& event = std::get<MetaEvent>(m.inner_event);
        if (event.type == MetaEventType::TempoEvent)
        {
            micros_per_qn = std::get<uint32_t>(event.event);
            tempos[ticks] = micros_per_qn;
            durations[ticks] = duration;
        }
        else if (event.type == MetaEventType::TimeSignature)
        {
            sigs[ticks] = std::get<TimeSignatureEvent>(event.event);
            durations[ticks] = duration;
        }
    }
    // calculate length for tracks extending past tempo map
    for (auto i = 1u; i < tracks_.size(); i++)
    {
        if (tracks_[i].total_ticks <= ticks) continue;
        long tmpTicks = 0;
        for (const auto& m : tracks_[i].events)
        {
            tmpTicks += m.delta_time;
            if (tmpTicks < ticks) continue;
            ticks += m.delta_time;
            duration += TicksToSeconds(ticks_per_qn_, m.delta_time, micros_per_qn);
        }
    }

    // more tempo+timesig map stuff
    // A MIDI file plays at 120 BPM in 4/4 until it sets its own tempo and time signature
    double bpm = 120.0;
    TimeSignatureEvent timesig{4, 2, 24, 8};
    // The import reads the map's first entry as the song's starting tempo and time signature
    durations.emplace(0, 0.0);
    for (const auto& [tick, time] : durations)
    {
        auto it_tempo = tempos.find(tick);
        auto it_sigs = sigs.find(tick);
        if (it_tempo != tempos.end())
        {
            bpm = TempoToBpm(it_tempo->second);
        }
        if (it_sigs != sigs.end())
        {
            timesig = it_sigs->second;
        }
        TimeSigTempoEvent tEvent;
        tEvent.time = time;
        tEvent.tick = tick;
        tEvent.bpm = bpm;
        tEvent.new_time_sig = tick == 0 || it_sigs != sigs.end();
        tEvent.new_tempo = tick == 0 || it_tempo != tempos.end();
        tEvent.numerator = timesig.numerator;
        tEvent.denominator = (1 << timesig.denominator);
        tempo_timesig_map_.emplace_back(tEvent);
    }
    return duration;
}