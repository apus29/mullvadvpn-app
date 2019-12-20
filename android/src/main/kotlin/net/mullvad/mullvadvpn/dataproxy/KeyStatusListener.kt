package net.mullvad.mullvadvpn.dataproxy

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import net.mullvad.mullvadvpn.model.KeygenEvent
import net.mullvad.mullvadvpn.service.MullvadDaemon

class KeyStatusListener(val daemon: MullvadDaemon) {
    private val setUpJob = setUp()

    var keyStatus: KeygenEvent? = null
        private set(value) {
            synchronized(this) {
                field = value

                if (value != null) {
                    onKeyStatusChange?.invoke(value)
                }
            }
        }

    var onKeyStatusChange: ((KeygenEvent) -> Unit)? = null
        set(value) {
            field = value

            synchronized(this) {
                keyStatus?.let { status -> value?.invoke(status) }
            }
        }

    private fun setUp() = GlobalScope.launch(Dispatchers.Default) {
        daemon.onKeygenEvent = { event -> keyStatus = event }
        val wireguardKey = daemon.getWireguardKey()
        if (wireguardKey != null) {
            keyStatus = KeygenEvent.NewKey(wireguardKey, null, null)
        }
    }

    fun generateKey() = GlobalScope.launch(Dispatchers.Default) {
        setUpJob.join()
        val oldStatus = keyStatus
        val newStatus = daemon.generateWireguardKey()
        val newFailure = newStatus?.failure()
        if (oldStatus is KeygenEvent.NewKey && newStatus != null) {
            keyStatus = KeygenEvent.NewKey(oldStatus.publicKey,
                            oldStatus.verified,
                            newFailure)
        } else {
            keyStatus = newStatus
        }
    }

    fun verifyKey() = GlobalScope.launch(Dispatchers.Default) {
        setUpJob.join()
        val verified = daemon.verifyWireguardKey()
        // Only update verification status if the key is actually there
        when (val state = keyStatus) {
            is KeygenEvent.NewKey -> {
                keyStatus = KeygenEvent.NewKey(state.publicKey,
                                verified,
                                state.replacementFailure)
            }
        }
    }

    fun onDestroy() {
        setUpJob.cancel()
        daemon.onKeygenEvent = null
    }

    private fun retryKeyGeneration() = GlobalScope.launch(Dispatchers.Default) {
        setUpJob.join()
        keyStatus = daemon.generateWireguardKey()
    }
}
