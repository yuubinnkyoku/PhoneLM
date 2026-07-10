package com.yuubinnkyoku.phonelm

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BenchmarkViewModelTest {
    @Test
    fun preventsDoubleStartAndForwardsStop() {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val stopped = AtomicBoolean(false)
        val engine = object : BenchmarkEngine {
            override fun environmentReport() = "mnn_version=test"

            override fun run(config: BenchmarkConfig, progress: (String) -> Unit): String {
                entered.countDown()
                release.await(5, TimeUnit.SECONDS)
                return "RESULT\nstatus=CANCELLED\nerror=stopped"
            }

            override fun requestStop() {
                stopped.set(true)
                release.countDown()
            }
        }
        val viewModel = BenchmarkViewModel(engine = engine, uiDispatcher = UiDispatcher { it() })

        assertTrue(viewModel.start(BenchmarkConfig.small()))
        assertTrue(entered.await(2, TimeUnit.SECONDS))
        assertFalse(viewModel.start(BenchmarkConfig.small()))
        assertTrue(viewModel.requestStop())
        assertTrue(stopped.get())

        viewModel.close()
    }
}

