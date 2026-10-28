<template>
    <a class="visually-hidden-focusable skip-link" href="#main-content">{{ $t('redesign.skip') }}</a>
    <header class="app-header border-bottom bg-body">
        <div class="d-flex align-items-center gap-2">
            <RouterLink
                class="navbar-brand d-flex align-items-center gap-2 fw-semibold"
                to="/"
                @click="expanded = false"
            >
                <img :src="headerLogo" class="brand-logo" alt="" width="36" height="36" />
                <span><span class="text-primary">Open</span>DTU-HEMS</span>
            </RouterLink>
            <HelpHint :label="$t('redesign.connection' + liveConnection)" plain>
                <template #trigger
                    ><span class="live-connection-dot" :data-state="liveConnection" aria-hidden="true"></span
                ></template>
                {{ $t('redesign.connection' + liveConnection) }}
            </HelpHint>
        </div>
        <ul class="d-flex align-items-center gap-2 list-unstyled mb-0">
            <ThemeSwitcher />
            <li><LocaleSwitcher /></li>
            <li>
                <button class="btn btn-outline-secondary btn-sm" type="button" @click="authenticate">
                    {{ $t(isLogged ? 'menu.Logout' : 'menu.Login') }}
                </button>
            </li>
            <li>
                <button
                    class="btn btn-outline-primary d-lg-none"
                    type="button"
                    :aria-expanded="expanded"
                    aria-controls="main-navigation"
                    :aria-label="$t('redesign.navigation')"
                    @click="expanded = !expanded"
                >
                    <BIconList />
                </button>
            </li>
        </ul>
    </header>
    <nav
        id="main-navigation"
        class="app-sidebar bg-body-tertiary border-end"
        :class="{ 'is-open': expanded }"
        :aria-label="$t('redesign.navigation')"
        @keydown.esc="expanded = false"
    >
        <section v-for="group in mainNavigation" :key="group.label" class="navigation-group">
            <h2 class="navigation-heading">{{ $t('redesign.' + group.label) }}</h2>
            <ul class="list-unstyled mb-0">
                <li v-for="item in group.items" :key="item.path" :class="{ 'active-parent': active(item) }">
                    <RouterLink
                        :to="item.path"
                        class="nav-link"
                        :class="{ 'nav-parent': item.children, active: !item.children && canonical === item.path }"
                        @click="expanded = false"
                    >
                        <component
                            :is="icons[item.label]"
                            v-if="icons[item.label]"
                            class="navigation-icon"
                            aria-hidden="true"
                        />
                        {{ $t('redesign.' + item.label) }}
                    </RouterLink>
                    <ul v-if="item.children && active(item)" class="nav-children list-unstyled">
                        <li v-for="child in item.children" :key="child.path">
                            <RouterLink
                                :to="child.path"
                                class="nav-link"
                                :class="{ active: canonical === child.path }"
                                @click="expanded = false"
                                >{{ $t('redesign.' + child.label) }}</RouterLink
                            >
                        </li>
                    </ul>
                </li>
            </ul>
        </section>
        <RouterLink to="/about" class="nav-link small" @click="expanded = false">{{ $t('menu.About') }}</RouterLink>
    </nav>
</template>
<script setup lang="ts">
import { computed, getCurrentInstance, onUnmounted, ref } from 'vue';
// Firmware embeds app.js but does not serve Vite's arbitrary /assets files.
import headerLogo from '@/assets/OpenDTU_HEMS_logo_transparent.svg?inline';
import { useRoute, useRouter } from 'vue-router';
import {
    BIconHouse,
    BIconList,
    BIconSun,
    BIconBatteryHalf,
    BIconEvStation,
    BIconPlug,
    BIconSpeedometer2,
    BIconGear,
    BIconCalendar3,
    BIconDiagram3,
    BIconActivity,
    BIconBarChart,
    BIconHdd,
    BIconWifi,
    BIconClock,
    BIconShield,
    BIconJournalText,
    BIconArrowRepeat,
    BIconPower,
    BIconFileText,
} from 'bootstrap-icons-vue';
import HelpHint from './HelpHint.vue';
import { useLiveData } from '@/utils/liveData';
import type { NavigationItem } from '@/utils/hemsNavigation';
import type { Component } from 'vue';
import { isLoggedIn, logout } from '@/utils/authentication';
import { mainNavigation } from '@/utils/hemsNavigation';
import LocaleSwitcher from './LocaleSwitcher.vue';
import ThemeSwitcher from './ThemeSwitcher.vue';
const expanded = ref(false),
    isLogged = ref(isLoggedIn());
const route = useRoute(),
    router = useRouter();
const { connection: liveConnection } = useLiveData();
const canonical = computed(() => route.matched.at(-1)?.aliasOf?.path || route.path);
const active = (item: NavigationItem): boolean => canonical.value === item.path || !!item.children?.some(active);
const icons: Record<string, Component> = {
    overview: BIconHouse,
    live: BIconBarChart,
    inverters: BIconHdd,
    solarCharger: BIconSun,
    battery: BIconBatteryHalf,
    wallbox: BIconEvStation,
    powerMeter: BIconSpeedometer2,
    planning: BIconCalendar3,
    powerControl: BIconActivity,
    integrations: BIconDiagram3,
    diagnostics: BIconActivity,
    network: BIconWifi,
    mqtt: BIconPlug,
    time: BIconClock,
    hardware: BIconHdd,
    security: BIconShield,
    logging: BIconJournalText,
    configuration: BIconFileText,
    firmware: BIconArrowRepeat,
    systemStatus: BIconGear,
    reboot: BIconPower,
};
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const updateLogin = () => {
    isLogged.value = isLoggedIn();
};
emitter.on('logged-in', updateLogin);
emitter.on('logged-out', updateLogin);
onUnmounted(() => {
    emitter.off('logged-in', updateLogin);
    emitter.off('logged-out', updateLogin);
});
function authenticate() {
    if (isLogged.value) {
        logout();
        emitter.emit('logged-out');
        router.push('/');
    } else router.push('/login');
}
</script>
