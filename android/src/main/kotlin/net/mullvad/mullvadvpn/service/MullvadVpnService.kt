package net.mullvad.mullvadvpn.service

import android.content.Intent
import android.os.Binder
import android.os.IBinder
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import net.mullvad.mullvadvpn.dataproxy.ConnectionProxy
import net.mullvad.talpid.TalpidVpnService
import net.mullvad.talpid.util.EventNotifier

class MullvadVpnService : TalpidVpnService() {
    private val binder = LocalBinder()

    private var isStopping = false

    private lateinit var daemon: Deferred<MullvadDaemon>
    private lateinit var connectionProxy: ConnectionProxy
    private lateinit var notificationManager: ForegroundNotificationManager

    private var serviceNotifier = EventNotifier<ServiceInstance?>(null)

    override fun onCreate() {
        super.onCreate()
        setUp()
    }

    override fun onBind(intent: Intent): IBinder {
        return super.onBind(intent) ?: binder
    }

    override fun onRebind(intent: Intent) {
        if (isStopping) {
            tearDown()
            setUp()
            isStopping = false
        }
    }

    override fun onUnbind(intent: Intent): Boolean {
        return true
    }

    override fun onDestroy() {
        tearDown()
        daemon.cancel()
        super.onDestroy()
    }

    inner class LocalBinder : Binder() {
        val serviceNotifier
            get() = this@MullvadVpnService.serviceNotifier

        fun stop() {
            this@MullvadVpnService.stop()
        }
    }

    private fun setUp() {
        daemon = startDaemon()
        connectionProxy = ConnectionProxy(this, daemon)
        notificationManager = startNotificationManager()
    }

    private fun startDaemon() = GlobalScope.async(Dispatchers.Default) {
        ApiRootCaFile().extract(application)

        val daemon = MullvadDaemon(this@MullvadVpnService).apply {
            onSettingsChange.subscribe { settings ->
                notificationManager.loggedIn = settings?.accountToken != null
            }
        }

        serviceNotifier.notify(ServiceInstance(daemon, connectionProxy, connectivityListener))

        daemon
    }

    private fun startNotificationManager(): ForegroundNotificationManager {
        return ForegroundNotificationManager(this, connectionProxy).apply {
            onConnect = { connectionProxy.connect() }
            onDisconnect = { connectionProxy.disconnect() }
        }
    }

    private fun stop() {
        isStopping = true

        serviceNotifier.notify(null)

        if (daemon.isCompleted) {
            runBlocking { daemon.await().shutdown() }
        } else {
            daemon.cancel()
        }

        stopSelf()
    }

    private fun tearDown() {
        connectionProxy.onDestroy()
        notificationManager.onDestroy()
    }
}
