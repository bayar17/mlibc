#include <errno.h>
#include <string.h>
#include <signal.h>
#include <mlibc/all-sysdeps.hpp>

#include "robu-abi.hpp"
#include "sysdeps_internal.hpp"

namespace robu_detail {

namespace {

constexpr int MAX_TRAMPOLINE_SIG = 32;
volatile int g_sig_interrupt_flag = 0;
void (*g_real_sig_handlers[MAX_TRAMPOLINE_SIG])(int) = {};

void robu_signal_trampoline(int signum) {
	g_sig_interrupt_flag = 1;
	if (signum > 0 && signum < MAX_TRAMPOLINE_SIG && g_real_sig_handlers[signum]) {
		g_real_sig_handlers[signum](signum);
	}
}

}

bool sig_check_interrupt() {
	if (!g_sig_interrupt_flag) {
		return false;
	}
	g_sig_interrupt_flag = 0;
	return true;
}

}

using namespace robu_detail;

namespace mlibc {

int Sysdeps<Sigaction>::operator()(int signum, const struct sigaction *act, struct sigaction *oldact) {
	uint64_t new_handler = 0, new_flags = 0, new_mask = 0;
	int do_set = act ? 1 : 0;
	uint64_t prev_real_handler = (signum > 0 && signum < robu_detail::MAX_TRAMPOLINE_SIG)
	                              ? reinterpret_cast<uint64_t>(robu_detail::g_real_sig_handlers[signum]) : 0;
	if (act) {
		uint64_t user_handler = (act->sa_flags & SA_SIGINFO)
		              ? reinterpret_cast<uint64_t>(act->sa_sigaction)
		              : reinterpret_cast<uint64_t>(act->sa_handler);
		if (!(act->sa_flags & SA_SIGINFO) && signum > 0 && signum < robu_detail::MAX_TRAMPOLINE_SIG &&
		    user_handler != (uint64_t)SIG_DFL && user_handler != (uint64_t)SIG_IGN) {
			robu_detail::g_real_sig_handlers[signum] = act->sa_handler;
			new_handler = reinterpret_cast<uint64_t>(&robu_detail::robu_signal_trampoline);
		} else {
			new_handler = user_handler;
		}
		new_flags = (uint64_t)act->sa_flags;
		new_mask = *reinterpret_cast<const uint64_t *>(&act->sa_mask);
	}
	uint64_t old_handler = 0, old_flags = 0, old_mask = 0;
	int64_t rc = robu::sig_action_raw(signum, new_handler, new_flags, new_mask, do_set,
	                                   &old_handler, &old_flags, &old_mask);
	if (rc != robu::IPC_ERR_NONE) {
		return EINVAL;
	}
	if (oldact) {
		memset(oldact, 0, sizeof(*oldact));
		uint64_t report_handler = old_handler;
		if (old_handler == reinterpret_cast<uint64_t>(&robu_detail::robu_signal_trampoline) &&
		    signum > 0 && signum < robu_detail::MAX_TRAMPOLINE_SIG) {
			report_handler = prev_real_handler;
		}
		if (old_flags & SA_SIGINFO) {
			oldact->sa_sigaction = reinterpret_cast<void (*)(int, siginfo_t *, void *)>(report_handler);
		} else {
			oldact->sa_handler = reinterpret_cast<void (*)(int)>(report_handler);
		}
		oldact->sa_flags = (int)old_flags;
		*reinterpret_cast<uint64_t *>(&oldact->sa_mask) = old_mask;
	}
	return 0;
}

int Sysdeps<Kill>::operator()(pid_t pid, int sig) {
	int64_t rc = robu::sig_kill_raw((uint64_t)pid, sig);
	return rc == robu::IPC_ERR_NONE ? 0 : ESRCH;
}

int Sysdeps<Tgkill>::operator()(int pid, int tid, int sig) {
	(void)pid;
	int64_t rc = robu::sig_kill_raw((uint64_t)tid, sig);
	return rc == robu::IPC_ERR_NONE ? 0 : ESRCH;
}

int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *set, sigset_t *retrieve) {
	uint64_t new_mask = set ? *reinterpret_cast<const uint64_t *>(set) : 0;
	uint64_t old_mask = 0;
	int64_t rc = robu::sig_procmask_raw((uint64_t)how, new_mask, set ? 1 : 0, &old_mask);
	if (rc != robu::IPC_ERR_NONE) {
		return EINVAL;
	}
	if (retrieve) {
		memset(retrieve, 0, sizeof(*retrieve));
		*reinterpret_cast<uint64_t *>(retrieve) = old_mask;
	}
	return 0;
}

int Sysdeps<Sigpending>::operator()(sigset_t *set) {
	uint64_t pending = robu::sig_pending_raw();
	if (set) {
		memset(set, 0, sizeof(*set));
		*reinterpret_cast<uint64_t *>(set) = pending;
	}
	return 0;
}

int Sysdeps<Sigsuspend>::operator()(const sigset_t *set) {
	uint64_t new_mask = set ? *reinterpret_cast<const uint64_t *>(set) : 0;
	uint64_t old_mask = 0;
	robu::sig_procmask_raw(SIG_SETMASK, new_mask, 1, &old_mask);
	robu::sleep_raw(0xFFFFFFFFULL);
	robu::sig_procmask_raw(SIG_SETMASK, old_mask, 1, nullptr);
	return EINTR;
}

}
