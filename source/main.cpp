// Witcher Senses Edit for Scavenger Sense. Own code, GPL-3.0-or-later (2026-11-03).
//
// A patch for Scavenger Sense (Nexus 188380, GPL-3.0-or-later, by KShakes). The owner, verbatim: "a small patch mod for
// Scavenger Sense that automates its button controls so that any target you hit with a bow, you automatically track
// it, and when it's dead, you stop tracking it."
//
// What the button does, read from Scavenger Sense's source (github.com/KinonNeko/ScavengerSense-repo, 0.10.1 beta) and
// confirmed in the shipped DLL's machine code: every tracking key ends in Sense::MarkUnderAim(), a toggle. It takes the
// actor in CrosshairPickData::target (falling back to a view pick), and marks them or, if already marked, releases them.
// Scavenger Sense exports no API, so this plugin calls that same function:
//
//   - A fake key press would not do. The default key is refused unless a sweep was opened crouching (TrailGateOpen /
//     reveal = crouchsense), the default all-in-one key fires a double-tap window later on whatever the aim is on by
//     then, and a toggle pressed blind releases a target that is already marked.
//   - So: on the same task queue Scavenger Sense polls on, the crosshair target is pointed at the actor for the length
//     of one synchronous call and put back, and the mark list is READ first, so a mark only ever marks and a release
//     only ever releases. All of Scavenger Sense's own work (colour, trail, toast, log) happens in its own code.
//
// Mark and release reach MarkUnderAim directly, so its callers' gates (TrailGateOpen, the reveal setting) do not apply. Its
// crosshair path has no range check and no alive check, so an arrow that lands from any distance marks; the dead are
// filtered here, and Tracks range only limits Scavenger Sense's own view pick, which this never reaches.
//
// Scavenger Sense has no idea of a mount, and activate on a horse is also Better Third Person Selection's hold to dismount.
// Sense::OnHotkey, the one place a sweep is asked for or faded, is hooked and does nothing while the player is on a mount.
//
// The owner's left trigger is also his hold modifier, and Scavenger Sense's press fires on the way down, so it cannot own that
// button. Its own gamepad key is off and this plugin calls OnHotkey for a quick tap: down and up inside fTapSeconds, with no
// other button touched in between. It runs from an input sink, the same place Scavenger Sense calls it from.
//
// The addresses belong to one build of one DLL, so they are checked before use: the PE timestamp, the image size and the
// first bytes of each function. Anything else and this plugin stays off and says so in its log. It never guesses.
#include "PCH.h"

#include "DevBench/DevBenchAPI.h"
#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"

#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>

#include <Windows.h>

namespace
{
	inline constexpr const char* kDisplayName = "Witcher Senses Edit for Scavenger Sense";
	inline constexpr const char* kLogName = "WitcherSensesEdit";
	inline constexpr const wchar_t* kTargetModule = L"ScavengerSense.dll";

	// ---- Scavenger Sense 0.10.1 beta, Nexus file 820549, sha256 e658b9f1...6ba6f. Read by disassembly on 2026-11-03. ----
	inline constexpr std::uint32_t kKnownTimestamp = 0x6AD0CB5D;
	inline constexpr std::uint32_t kKnownImageSize = 0x14F000;

	struct Known
	{
		std::uint32_t rva;
		std::array<std::uint8_t, 16> head;
		const char* name;
	};

	inline constexpr Known kSenseGetSingleton{ 0x81090, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x0D, 0x0C, 0x21, 0x0C, 0x00, 0x65, 0x48, 0x8B, 0x04 }, "Sense::GetSingleton" };
	inline constexpr Known kMarkUnderAim{ 0x827B0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8D }, "Sense::MarkUnderAim" };
	inline constexpr Known kPerksGetSingleton{ 0x689A0, { 0x48, 0x83, 0xEC, 0x28, 0x8B, 0x0D, 0xFE, 0xA7, 0x0D, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58 }, "Perks::GetSingleton" };
	inline constexpr Known kAllowsTracking{ 0x67A10, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x59, 0x08, 0x48, 0x85, 0xDB, 0x74, 0x1F, 0xE8 }, "Perks::AllowsTracking" };
	inline constexpr Known kOnHotkey{ 0x83A80, { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00 }, "Sense::OnHotkey" };
	// OnHotkey's first instruction, mov [rsp+20h], rbx, is exactly the five bytes the jump replaces.
	inline constexpr std::size_t kOnHotkeyStolen = 5;

	// Sense::_marked, a std::unordered_map<FormID, Marked>: the list head is at +0x260, the size at +0x268 (read from
	// the erase in MarkUnderAim's toggle). MSVC's list node is { next, prev, value }, so a node's key is at +0x10.
	inline constexpr std::size_t kMarkedHead = 0x260;
	inline constexpr std::size_t kMarkedSize = 0x268;
	inline constexpr std::size_t kNodeKey = 0x10;
	// CrosshairPickData::target, at +4 on every flat runtime: the offset MarkUnderAim itself reads.
	inline constexpr std::size_t kPickTarget = 0x4;

	using GetSingletonFn = void* (*)();
	using MemberBoolFn = bool (*)(void*);

	GetSingletonFn g_senseSingleton = nullptr;
	MemberBoolFn g_markUnderAim = nullptr;
	GetSingletonFn g_perksSingleton = nullptr;
	MemberBoolFn g_allowsTracking = nullptr;
	using MemberVoidFn = void (*)(void*);
	MemberVoidFn g_onHotkey = nullptr;

	// Rule 31: what the patch decided, for DevBench and the log. Everything is plain and cheap.
	std::atomic<bool> g_ready{ false };
	float g_tapSeconds = 0.25f;
	std::atomic<std::uint32_t> g_bowHits{ 0 }, g_marked{ 0 }, g_alreadyMarked{ 0 }, g_deadAtMark{ 0 }, g_released{ 0 }, g_failed{ 0 }, g_perkRefused{ 0 }, g_mountRefused{ 0 };
	std::atomic<RE::FormID> g_lastTarget{ 0 };
	std::mutex g_statusLock;
	std::string g_status = "not checked yet";

	void SetStatus(std::string a_text)
	{
		std::scoped_lock l(g_statusLock);
		g_status = std::move(a_text);
	}

	// On a mount, or part way on or off anything.
	bool PlayerOnMount()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) { return false; }
		if (player->IsOnMount()) { return true; }
		const auto state = player->AsActorState()->GetSitSleepState();
		return state == RE::SIT_SLEEP_STATE::kWantToSit || state == RE::SIT_SLEEP_STATE::kWaitingForSitAnim || state == RE::SIT_SLEEP_STATE::kWantToStand;
	}

	// In front of Sense::OnHotkey. A sense already on stays on: the fade is behind this too.
	void OnHotkeyHook(void* a_sense)
	{
		if (PlayerOnMount())
		{
			++g_mountRefused;
			logger::debug("sweep: on a mount, the sense key does nothing");
			return;
		}
		g_onHotkey(a_sense);
	}

	// A free page within a rel32 jump of a_target, searched outward from it. Null when there is none.
	std::uint8_t* AllocateNear(std::uintptr_t a_target, std::size_t a_size)
	{
		constexpr std::uintptr_t kStep = 0x10000;
		constexpr std::uintptr_t kReach = 0x70000000;
		const std::uintptr_t origin = a_target & ~(kStep - 1);
		for (std::uintptr_t offset = kStep; offset < kReach; offset += kStep)
		{
			for (const std::uintptr_t address : { origin + offset, origin - offset })
			{
				MEMORY_BASIC_INFORMATION info{};
				if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_FREE || info.RegionSize < a_size) { continue; }
				if (void* page = VirtualAlloc(reinterpret_cast<void*>(address), a_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) { return static_cast<std::uint8_t*>(page); }
			}
		}
		return nullptr;
	}

	// A jump at OnHotkey's entry to a page near Scavenger Sense's DLL: one far jump to the hook, then the replaced instruction
	// and a far jump back. Our DLL and SKSE's trampoline can be over 2 GB from it, which is why neither can be the jump's target
	// (2026-11-03: SKSE::Trampoline's write_branch stopped the game at load with "displacement is out of range"). No page: no gate.
	void InstallMountGate(std::uintptr_t a_base)
	{
		const std::uintptr_t entry = a_base + kOnHotkey.rva;
		auto* page = AllocateNear(entry, 0x1000);
		if (!page)
		{
			logger::warn("mount gate: no free memory near Scavenger Sense; a hold on a horse will still run the sense");
			return;
		}
		const std::uint8_t farJump[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
		const auto writeFarJump = [&farJump](std::uint8_t* a_at, std::uintptr_t a_to) {
			std::memcpy(a_at, farJump, sizeof(farJump));
			std::memcpy(a_at + sizeof(farJump), &a_to, sizeof(a_to));
		};
		writeFarJump(page, reinterpret_cast<std::uintptr_t>(&OnHotkeyHook));
		auto* stub = page + 14;
		std::memcpy(stub, reinterpret_cast<const void*>(entry), kOnHotkeyStolen);
		writeFarJump(stub + kOnHotkeyStolen, entry + kOnHotkeyStolen);
		g_onHotkey = reinterpret_cast<MemberVoidFn>(stub);
		FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
		std::uint8_t jump[5]{ 0xE9 };
		const auto disp = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(page) - static_cast<std::intptr_t>(entry + sizeof(jump)));
		std::memcpy(jump + 1, &disp, sizeof(disp));
		DWORD old = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(entry), sizeof(jump), PAGE_EXECUTE_READWRITE, &old))
		{
			logger::warn("mount gate: could not write the jump; a hold on a horse will still run the sense");
			return;
		}
		std::memcpy(reinterpret_cast<void*>(entry), jump, sizeof(jump));
		VirtualProtect(reinterpret_cast<void*>(entry), sizeof(jump), old, &old);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(entry), sizeof(jump));
		logger::info("mount gate: on");
	}

	// The left trigger as a quick tap. Any other button pressed while it is down, or still down when it goes down, makes it a modifier.
	class TapSink final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static TapSink* GetSingleton()
		{
			static TapSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_events) { return RE::BSEventNotifyControl::kContinue; }
			for (auto* e = *a_events; e; e = e->next)
			{
				const auto* button = e->AsButtonEvent();
				if (!button || button->GetDevice() != RE::INPUT_DEVICE::kGamepad) { continue; }
				if (button->GetIDCode() != RE::BSWin32GamepadDevice::Key::kLeftTrigger)
				{
					if (button->IsPressed()) { _armed = false; }
					continue;
				}
				if (button->IsDown())
				{
					_armed = GameplayHasInput();
				}
				else if (button->IsUp())
				{
					const bool tap = _armed && button->HeldDuration() <= g_tapSeconds && GameplayHasInput();
					logger::debug("tap: left trigger up after {:.2f}s, {}", button->HeldDuration(), tap ? "a tap" : "not a tap");
					_armed = false;
					if (tap) { OnHotkeyHook(g_senseSingleton()); }
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		// Scavenger Sense's own gate on its key.
		static bool GameplayHasInput()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::Console::MENU_NAME);
		}

		bool _armed = false;
	};

	// Checks everything before a single address is trusted. Scavenger Sense is a beta that updates; an update leaves this
	// patch switched off with a line in the log rather than calling into the wrong code.
	void BindScavengerSense()
	{
		const HMODULE module = GetModuleHandleW(kTargetModule);
		if (!module)
		{
			SetStatus("ScavengerSense.dll is not loaded");
			logger::warn("off: ScavengerSense.dll is not loaded. Is Scavenger Sense installed, and did it load (its own DLL needs SKSE Menu Framework)?");
			return;
		}
		const auto base = reinterpret_cast<std::uintptr_t>(module);
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const std::uint32_t stamp = nt->FileHeader.TimeDateStamp;
		const std::uint32_t size = nt->OptionalHeader.SizeOfImage;
		if (stamp != kKnownTimestamp || size != kKnownImageSize)
		{
			SetStatus(std::format("unknown Scavenger Sense build (timestamp {:08X}, size {:X})", stamp, size));
			logger::error("off: this Scavenger Sense build is not the one this patch was made for (timestamp {:08X}, size {:X}; expected {:08X}, {:X}). "
						  "It does nothing until it is updated for that build.", stamp, size, kKnownTimestamp, kKnownImageSize);
			return;
		}
		for (const Known* k : { &kSenseGetSingleton, &kMarkUnderAim, &kPerksGetSingleton, &kAllowsTracking, &kOnHotkey })
		{
			if (std::memcmp(reinterpret_cast<const void*>(base + k->rva), k->head.data(), k->head.size()) != 0)
			{
				SetStatus(std::format("{} does not start with the expected bytes", k->name));
				logger::error("off: {} at +{:X} does not start with the expected bytes; something has patched or replaced the DLL", k->name, k->rva);
				return;
			}
		}
		g_senseSingleton = reinterpret_cast<GetSingletonFn>(base + kSenseGetSingleton.rva);
		g_markUnderAim = reinterpret_cast<MemberBoolFn>(base + kMarkUnderAim.rva);
		g_perksSingleton = reinterpret_cast<GetSingletonFn>(base + kPerksGetSingleton.rva);
		g_allowsTracking = reinterpret_cast<MemberBoolFn>(base + kAllowsTracking.rva);
		g_onHotkey = reinterpret_cast<MemberVoidFn>(base + kOnHotkey.rva);
		InstallMountGate(base);
		g_ready.store(true);
		SetStatus("on");
		logger::info("on: Scavenger Sense 0.10.1 beta recognised (timestamp {:08X}); arrows you land now mark their target, and no sweep on a mount", stamp);
	}

	// Reads the mark list. Only ever called on the task queue Scavenger Sense itself polls on, so it cannot be mid-change.
	bool IsMarked(RE::FormID a_id)
	{
		auto* sense = static_cast<std::byte*>(g_senseSingleton());
		auto* head = *reinterpret_cast<std::byte**>(sense + kMarkedHead);
		const auto size = *reinterpret_cast<std::size_t*>(sense + kMarkedSize);
		if (!head) { return false; }
		auto* node = *reinterpret_cast<std::byte**>(head);
		for (std::size_t i = 0; i < size && node && node != head; ++i)
		{
			if (*reinterpret_cast<RE::FormID*>(node + kNodeKey) == a_id) { return true; }
			node = *reinterpret_cast<std::byte**>(node);
		}
		return false;
	}

	// The press itself: the crosshair on this actor, for exactly as long as the mod's own function takes.
	bool PressFor(RE::Actor* a_actor)
	{
		auto* pick = RE::CrosshairPickData::GetSingleton();
		if (!pick)
		{
			logger::warn("press: the game has no crosshair data yet");
			return false;
		}
		auto* slot = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::byte*>(pick) + kPickTarget);
		const std::uint32_t saved = *slot;
		*slot = a_actor->CreateRefHandle().native_handle();
		const bool acted = g_markUnderAim(g_senseSingleton());
		*slot = saved;
		return acted;
	}

	const char* NameOf(RE::Actor* a_actor)
	{
		const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
		return name && name[0] ? name : "?";
	}

	void MarkNow(RE::ActorHandle a_handle)
	{
		if (!g_ready.load()) { return; }
		auto actor = a_handle.get();
		if (!actor)
		{
			logger::debug("mark: the target is gone from the world");
			return;
		}
		const RE::FormID id = actor->GetFormID();
		g_lastTarget.store(id);
		// One arrow can kill. Marking the corpse would only make a mark to take straight back off.
		if (actor->IsDead())
		{
			++g_deadAtMark;
			logger::debug("mark: {} ({:08X}) was already dead when the hit came through", NameOf(actor.get()), id);
			return;
		}
		if (IsMarked(id))
		{
			++g_alreadyMarked;
			logger::debug("mark: {} ({:08X}) is already tracked; the press is not sent, it would release them", NameOf(actor.get()), id);
			return;
		}
		// The one gate of the key's that still applies: a player can name a perk that tracking requires.
		if (!g_allowsTracking(g_perksSingleton()))
		{
			++g_perkRefused;
			logger::debug("mark: {} ({:08X}): the perk Scavenger Sense requires for tracking is not held", NameOf(actor.get()), id);
			return;
		}
		PressFor(actor.get());
		if (IsMarked(id))
		{
			++g_marked;
			logger::info("mark: arrow hit, now tracking {} ({:08X})", NameOf(actor.get()), id);
		}
		else
		{
			++g_failed;
			logger::warn("mark: pressed for {} ({:08X}) but they are not on the mark list afterwards", NameOf(actor.get()), id);
		}
	}

	void ReleaseNow(RE::ActorHandle a_handle)
	{
		if (!g_ready.load()) { return; }
		auto actor = a_handle.get();
		if (!actor) { return; }
		const RE::FormID id = actor->GetFormID();
		// Not on the list covers everybody who was never marked, and Scavenger Sense's own death release getting there first.
		if (!IsMarked(id)) { return; }
		PressFor(actor.get());
		if (!IsMarked(id))
		{
			++g_released;
			logger::info("release: {} ({:08X}) is dead, no longer tracking them", NameOf(actor.get()), id);
		}
		else
		{
			++g_failed;
			logger::warn("release: pressed for {} ({:08X}) but they are still on the mark list", NameOf(actor.get()), id);
		}
	}

	void Queue(void (*a_fn)(RE::ActorHandle), RE::ActorHandle a_handle)
	{
		if (auto* tasks = SKSE::GetTaskInterface())
		{
			tasks->AddTask([a_fn, a_handle]() { a_fn(a_handle); });
		}
	}

	bool IsPlayerArrow(const RE::TESHitEvent& a_event)
	{
		// The projectile is no test. A hit on an actor comes with the bow as source and projectile 0 (the two actors shot in the
		// 2026-11-03 log, an elk and a guard); only a hit on a non-actor names the arrow. A bash is the one non-arrow hit that
		// has a bow as its source, and it says so in its flags.
		if (!a_event.cause || !a_event.cause->IsPlayerRef()) { return false; }
		// Scavenger Sense itself keeps Bow and Crossbow as two kinds, and the owner asked for a bow: a bolt does not count.
		if (a_event.flags.any(RE::TESHitEvent::Flag::kBashAttack)) { return false; }
		const auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_event.source);
		return weapon && weapon->IsBow();
	}

	class Sinks final : public RE::BSTEventSink<RE::TESHitEvent>, public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		static Sinks* GetSingleton()
		{
			static Sinks singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			// Every hit the player lands, in the debug log: the report for "an arrow marked nothing" has to show what the game sent.
			if (a_event && a_event->cause && a_event->cause->IsPlayerRef())
			{
				logger::debug("hit: target {:08X} source {:08X} projectile {:08X} flags {:X} -> {}", a_event->target ? a_event->target->GetFormID() : 0,
					a_event->source, a_event->projectile, a_event->flags.underlying(), IsPlayerArrow(*a_event) ? "a bow arrow" : "not a bow arrow");
			}
			if (a_event && a_event->target && IsPlayerArrow(*a_event))
			{
				auto* actor = a_event->target->As<RE::Actor>();
				if (!actor)
				{
					// Not only walls: another mod's dynamic references report a hit every frame while an arrow is in them.
					const auto* base = a_event->target->GetBaseObject();
					const auto* file = base ? base->GetFile(0) : nullptr;
					logger::debug("skip: target {:08X} is a {}, not an actor (base {:08X} {} from {})", a_event->target->GetFormID(),
						RE::FormTypeToString(a_event->target->GetFormType()), base ? base->GetFormID() : 0,
						base ? RE::FormTypeToString(base->GetFormType()) : "none"sv, file ? file->GetFilename() : "no file"sv);
				}
				else if (!actor->IsPlayerRef())
				{
					++g_bowHits;
					Queue(MarkNow, actor->CreateRefHandle());
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		// Fires for dying and again for dead; ReleaseNow does nothing the second time.
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (a_event && a_event->actorDying)
			{
				if (auto* actor = a_event->actorDying->As<RE::Actor>())
				{
					Queue(ReleaseNow, actor->CreateRefHandle());
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	std::string StateJson()
	{
		std::string status;
		{
			std::scoped_lock l(g_statusLock);
			status = g_status;
		}
		for (auto& c : status)
		{
			if (c == '"' || c == '\\') { c = '\''; }
		}
		return std::format(
			"{{\"ok\":true,\"mod\":\"{}\",\"ready\":{},\"status\":\"{}\",\"bowHits\":{},\"marked\":{},\"alreadyMarked\":{},\"deadAtMark\":{},"
			"\"released\":{},\"perkRefused\":{},\"mountRefused\":{},\"failed\":{},\"lastTarget\":\"{:08X}\"}}",
			kDisplayName, g_ready.load() ? "true" : "false", status, g_bowHits.load(), g_marked.load(), g_alreadyMarked.load(), g_deadAtMark.load(),
			g_released.load(), g_perkRefused.load(), g_mountRefused.load(), g_failed.load(), g_lastTarget.load());
	}

	// Every effect shader playing in the world, whoever started it, written to the log: "this object is still glowing" needs to
	// show whether the effect is alive, finished but not gone, or nothing to do with Scavenger Sense. Scavenger Sense's own pool
	// of shaders all come from ScavengerSense.esp, so the file name says whose it is.
	void LogEffects()
	{
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) { return; }
		std::uint32_t count = 0;
		lists->ForEachShaderEffect([&count](RE::ShaderReferenceEffect* a_effect) {
			if (!a_effect) { return RE::BSContainer::ForEachResult::kContinue; }
			++count;
			const auto target = a_effect->target.get();
			const auto* shader = a_effect->effectData;
			const auto* file = shader ? shader->GetFile(0) : nullptr;
			logger::info("effect: {:08X} on {:08X} ({}) from {}: age {:.1f} of {:.1f}, finished {}", shader ? shader->GetFormID() : 0, target ? target->GetFormID() : 0,
				target ? target->GetName() : "gone", file ? file->GetFilename() : "no file"sv, a_effect->age, a_effect->lifetime, a_effect->finished);
			return RE::BSContainer::ForEachResult::kContinue;
		});
		logger::info("effect: {} playing", count);
	}

	// op=mark/release with formId drive the same two paths a real hit and a real death take. op=effects logs every effect shader. Without an op: the state.
	void DevBenchHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		const std::string args = a_argsJson ? a_argsJson : "";
		if (args.find("\"effects\"") != std::string::npos)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { LogEffects(); }); }
		}
		const bool mark = args.find("\"mark\"") != std::string::npos;
		const bool release = args.find("\"release\"") != std::string::npos;
		if (mark || release)
		{
			const auto at = args.find("\"formId\"");
			const auto quote = at == std::string::npos ? at : args.find('"', args.find(':', at));
			RE::FormID id = 0;
			if (quote != std::string::npos)
			{
				try { id = static_cast<RE::FormID>(std::stoul(args.substr(quote + 1), nullptr, 16)); } catch (...) { id = 0; }
			}
			auto* actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
			if (!actor)
			{
				a_write(a_sink, R"({"ok":false,"error":"need formId, the hex FormID of an actor, as a string"})");
				return;
			}
			Queue(mark ? MarkNow : ReleaseNow, actor->CreateRefHandle());
		}
		a_write(a_sink, StateJson().c_str());
	}

	void RegisterDevBench(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::debug("DevBench not detected; no state tool"); }
			return;
		}
		constexpr const char* descriptor =
			"{\"description\":\"Witcher Senses Edit for Scavenger Sense. Default: whether the patch recognised Scavenger Sense, and counts of "
			"bow hits, marks, releases, holds refused on a mount and failures. op=mark or op=release with formId (hex string) queues the same path a player arrow or "
			"a death takes for that actor. op=effects writes every effect shader playing in the world to the log.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"formId\":{\"type\":\"string\"}}},"
			"\"readOnly\":false}";
		registered = devBench->RegisterTool("witchersenses.state", descriptor, &DevBenchHandler, nullptr);
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			RegisterDevBench(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			RegisterDevBench(true);
			BindScavengerSense();
			if (g_ready.load())
			{
				if (auto* events = RE::ScriptEventSourceHolder::GetSingleton())
				{
					events->AddEventSink<RE::TESHitEvent>(Sinks::GetSingleton());
					events->AddEventSink<RE::TESDeathEvent>(Sinks::GetSingleton());
					logger::info("listening for hits and deaths");
					auto* input = RE::BSInputDeviceManager::GetSingleton();
					if (input && g_tapSeconds > 0.0f)
					{
						input->AddEventSink<RE::InputEvent*>(TapSink::GetSingleton());
						logger::info("left trigger: a tap of up to {:.2f}s runs the sense", g_tapSeconds);
					}
				}
				else
				{
					g_ready.store(false);
					SetStatus("no event source");
					logger::error("off: the game's event source holder is not available");
				}
			}
			break;
		default:
			break;
		}
	}

	void LoadTapSeconds()
	{
		const auto ini = std::filesystem::absolute("Data/SKSE/Plugins/WitcherSensesEdit.ini");
		wchar_t text[32]{};
		GetPrivateProfileStringW(L"Sense", L"fTapSeconds", L"0.25", text, 32, ini.c_str());
		g_tapSeconds = static_cast<float>(_wtof(text));
	}

	spdlog::level::level_enum ConfiguredLogLevel()
	{
		const auto ini = std::filesystem::absolute("Data/SKSE/Plugins/WitcherSensesEdit.ini");
		const UINT level = GetPrivateProfileIntW(L"Debug", L"uLogLevel", 2, ini.c_str());
		return static_cast<spdlog::level::level_enum>(level <= 6 ? level : 2);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::log::init(kLogName);
	if (!AddressLibraryGuard::Guard(kDisplayName))
	{
		return true;
	}
	SKSE::Init(a_skse);
	const auto level = ConfiguredLogLevel();
	SKSE::log::set_level(level, level);
	LoadTapSeconds();
	logger::info("{} {} loading", kDisplayName, SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));
	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
	return true;
}
