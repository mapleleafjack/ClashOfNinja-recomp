#include "devices.h"

#include <algorithm>
#include <cstdlib>

#include "util.h"

namespace nla {

void DeviceMap::clear() {
    devices_.clear();
    keyboard_pad_ = 1;
    note_.clear();
}

void DeviceMap::load(const std::wstring& listing) {
    for (const auto& line : split(listing, L'\n')) {
        auto fields = split(line, L'\t');
        if (fields.size() >= 2 && fields[0] == L"keyboard") {
            keyboard_pad_ = std::clamp(std::atoi(narrow(fields[1]).c_str()), 1, kPads);
        } else if (fields.size() >= 4 && fields[0] == L"device") {
            DeviceRow device;
            device.nth = std::atoi(narrow(fields[1]).c_str());
            device.pad = std::atoi(narrow(fields[2]).c_str());
            device.name = fields[3];
            devices_.push_back(device);
        }
    }
}

std::vector<int> DeviceMap::resolved_pads() const {
    std::vector<int> pads(devices_.size(), 0);
    bool taken[kPads + 1] = {};
    if (keyboard_pad_ >= 1 && keyboard_pad_ <= kPads) taken[keyboard_pad_] = true;
    for (size_t i = 0; i < devices_.size(); ++i) {
        const int pin = devices_[i].pad;
        if (pin < 1 || pin > kPads || taken[pin]) continue;
        taken[pin] = true;
        pads[i] = pin;
    }
    for (size_t i = 0; i < devices_.size(); ++i) {
        for (int free = 1; free <= kPads && pads[i] == 0; ++free) {
            if (taken[free]) continue;
            taken[free] = true;
            pads[i] = free;
        }
    }
    return pads;
}

int DeviceMap::player_device(int player) const {
    const std::vector<int> pads = resolved_pads();
    for (size_t i = 0; i < pads.size(); ++i)
        if (pads[i] == player) return static_cast<int>(i);
    return -1;
}

bool DeviceMap::player_is_keyboard(int player) const {
    return keyboard_pad_ == player && player >= 1 && player <= kPads;
}

std::wstring DeviceMap::player_device_label(int player) const {
    if (player_is_keyboard(player)) return L"Keyboard";
    const int device = player_device(player);
    if (device < 0) return L"Not set";
    std::wstring label = devices_[static_cast<size_t>(device)].name;
    if (devices_[static_cast<size_t>(device)].nth > 1)
        label += L" #" + std::to_wstring(devices_[static_cast<size_t>(device)].nth);
    return label;
}

std::vector<std::wstring> DeviceMap::pad_options() const {
    constexpr std::size_t kMost = static_cast<std::size_t>(static_cast<int>(Id::PadOptionEnd) -
                                                           static_cast<int>(Id::PadOption));
    std::vector<std::wstring> options{L"Not set", L"Keyboard"};
    for (size_t i = 0; i < devices_.size() && options.size() < kMost; ++i) {
        std::wstring label = devices_[i].name;
        if (devices_[i].nth > 1) label += L" #" + std::to_wstring(devices_[i].nth);
        options.push_back(label);
    }
    return options;
}

void DeviceMap::assign(int player, int choice) {
    if (player < 1 || player > kPads) return;
    const int pad = player;
    auto displace_keyboard = [&] {
        if (keyboard_pad_ != pad) return;
        for (int candidate = 1; candidate <= kPads; ++candidate) {
            if (candidate == pad) continue;
            bool taken = false;
            for (const auto& device : devices_)
                if (device.pad == candidate) taken = true;
            if (!taken) {
                keyboard_pad_ = candidate;
                return;
            }
        }
    };
    if (choice == 1) {  // the keyboard
        for (auto& device : devices_)
            if (device.pad == pad) device.pad = 0;
        keyboard_pad_ = pad;
        return;
    }
    const size_t index = choice >= 2 ? static_cast<size_t>(choice - 2) : devices_.size();
    if (index < devices_.size() && devices_[index].pad == pad) return;  // already here
    for (auto& device : devices_)
        if (device.pad == pad) device.pad = 0;
    displace_keyboard();
    if (index < devices_.size()) devices_[index].pad = pad;
}

}  // namespace nla
