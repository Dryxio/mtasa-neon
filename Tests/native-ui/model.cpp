#include "NativeUIModel.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace NativeUI;
int main()
{
    Model       m;
    int         a{}, b{};
    std::string error, encoded;
    assert(Encode("Été à Los Santos", encoded) && static_cast<unsigned char>(encoded[0]) == 0x87 && static_cast<unsigned char>(encoded[2]) == 0x9E);
    assert(!Encode("emoji 😀", encoded));
    assert(!Encode(std::string("a\0b", 3), encoded));
    assert(!Encode("[atlas]", encoded));
    assert(!Encode(std::string(161, 'x'), encoded));
    assert(!Encode("\xC3", encoded));
    assert(Tokens("~r~Salut~s~~n~~k~~PED_SPRINT~"));
    assert(!Tokens("~k~~NOT_A_CONTROL~"));
    assert(!Tokens("~q~"));
    assert(!Tokens("~r"));
    assert(Numbers("~1~ / ~2~ (encore ~1~)", {{5, 20}}) == "5 / 20 (encore 5)");
    SNativeUIOptions t;
    t.name = "label";
    t.content = "Chrono";
    auto first = m.Insert(&a, ENativeUIKind::Text, t, 0, error);
    assert(first);
    assert(!m.Find(&b, first));
    assert(!m.Insert(&a, ENativeUIKind::Text, t, 0, error));
    assert(m.Insert(&b, ENativeUIKind::Text, t, 0, error));
    SNativeUIOptions clock;
    clock.text = first;
    clock.value = 2500;
    clock.beepSeconds = 12;
    auto timer = m.Insert(&a, ENativeUIKind::Clock, clock, 100, error);
    assert(timer);
    assert(!m.Insert(&b, ENativeUIKind::Clock, clock, 100, error));
    assert(!m.Insert(&a, ENativeUIKind::Clock, clock, 100, error));
    auto* entry = m.Find(&a, timer);
    assert(Model::Tick(*entry, 700));
    assert(entry->options.value == 1900);
    entry->options.paused = true;
    assert(!Model::Tick(*entry, 3000));
    assert(entry->options.value == 1900);
    entry->options.paused = false;
    Model::Tick(*entry, 5000);
    assert(entry->finished && entry->options.value == 0);
    Model::Tick(*entry, 10000);
    assert(entry->options.value == 0);
    assert(m.Referenced(&a, first));
    m.entries.erase(timer);
    assert(!m.Referenced(&a, first));
    auto second = m.Insert(&a, ENativeUIKind::Clock, clock, 0, error);
    assert(second > timer && !m.Find(&a, timer));
    SNativeUIOptions counter;
    counter.color = 0;
    counter.style = 1;
    counter.value = 50;
    counter.maximum = 100;
    for (int i = 0; i < 4; ++i)
        assert(m.Insert(&a, ENativeUIKind::Counter, counter, 0, error));
    assert(!m.Insert(&a, ENativeUIKind::Counter, counter, 0, error));
    counter.value = 101;
    assert(!m.Validate(&a, ENativeUIKind::Counter, counter, error));
    counter.value = std::numeric_limits<double>::quiet_NaN();
    assert(!m.Validate(&a, ENativeUIKind::Counter, counter, error));
    SNativeUIOptions menu;
    menu.cells = {first, first};
    menu.columns = 2;
    menu.width = 150;
    menu.enabled = {true};
    assert(m.Insert(&a, ENativeUIKind::Menu, menu, 0, error));
    assert(m.Insert(&a, ENativeUIKind::Menu, menu, 0, error));
    assert(!m.Insert(&a, ENativeUIKind::Menu, menu, 0, error));
    menu.enabled = {false};
    assert(!m.Validate(&a, ENativeUIKind::Menu, menu, error));
    menu.enabled = {true};
    menu.widths = {500, 500};
    assert(!m.Validate(&a, ENativeUIKind::Menu, menu, error));
    Model exhausted;
    exhausted.next = 0x1000000;
    assert(!exhausted.Insert(&a, ENativeUIKind::Text, t, 0, error));
    assert(!Tokens("~1~~1~~1~~1~~1~~1~~1~"));
    Model            drawing;
    auto             label = drawing.Insert(&a, ENativeUIKind::Text, t, 0, error);
    SNativeUIOptions draw;
    draw.text = label;
    draw.visible = false;
    for (int i = 0; i < 96; ++i)
        assert(drawing.Insert(&a, ENativeUIKind::DrawText, draw, 0, error));
    assert(!drawing.Insert(&a, ENativeUIKind::Card, draw, 0, error));
    SNativeUIOptions card;
    card.card = 54;
    assert(!drawing.Validate(&a, ENativeUIKind::Card, card, error));
    Model total;
    for (int i = 0; i < 256; ++i)
    {
        t.name = "text-" + std::to_string(i);
        assert(total.Insert(&a, ENativeUIKind::Text, t, 0, error));
    }
    t.name = "overflow";
    assert(!total.Insert(&a, ENativeUIKind::Text, t, 0, error));
    Entry elapsed;
    elapsed.kind = ENativeUIKind::Clock;
    elapsed.options.countdown = false;
    elapsed.options.value = 5999990;
    elapsed.tick = 100;
    Model::Tick(elapsed, 5000);
    assert(elapsed.options.value == 5999999);
    Entry corrected;
    corrected.kind = ENativeUIKind::Clock;
    corrected.options.value = 500;
    Model::Tick(corrected, 600);
    assert(Model::TakeCompletion(corrected));
    assert(!Model::TakeCompletion(corrected));
    corrected.options.value = 2000;
    corrected.finished = false;
    Model::Tick(corrected, 2700);
    assert(corrected.finished);
    assert(!Model::TakeCompletion(corrected));
    Entry fresh;
    fresh.kind = ENativeUIKind::Clock;
    Model::Tick(fresh, 1);
    assert(Model::TakeCompletion(fresh));
    std::cout << "native-ui model: ownership, quotas, stale generations, clocks, encoding, tokens, layouts PASS\n";
}
