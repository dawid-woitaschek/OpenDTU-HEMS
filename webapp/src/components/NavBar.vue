<template>
    <a class="visually-hidden-focusable skip-link" href="#main-content">{{ $t('redesign.skip') }}</a>
    <header class="app-header border-bottom bg-body">
        <RouterLink class="navbar-brand d-flex align-items-center gap-2 fw-semibold" to="/" @click="expanded = false">
            <BIconHouse class="brand-logo text-primary" aria-hidden="true" />
            <span><span class="text-primary">Open</span>DTU-HEMS</span>
        </RouterLink>
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
        <template v-for="group in mainNavigation" :key="group.label">
            <details v-if="group.label === 'system'" class="navigation-group" :open="systemOpen">
                <summary class="navigation-heading">{{ $t('redesign.' + group.label) }}</summary>
                <RouterLink
                    v-for="item in group.items"
                    :key="item.path"
                    :to="item.path"
                    class="nav-link"
                    @click="expanded = false"
                    >{{ $t('redesign.' + item.label) }}</RouterLink
                >
                <RouterLink to="/settings/device" class="nav-link" @click="expanded = false">{{
                    $t('menu.DeviceManager')
                }}</RouterLink>
                <RouterLink to="/info/console" class="nav-link" @click="expanded = false">{{
                    $t('menu.Console')
                }}</RouterLink>
            </details>
            <section v-else class="navigation-group">
                <h2 class="navigation-heading">{{ $t('redesign.' + group.label) }}</h2>
                <RouterLink
                    v-for="item in group.items"
                    :key="item.path"
                    :to="item.path"
                    class="nav-link"
                    exact-active-class="active"
                    @click="expanded = false"
                    >{{ $t('redesign.' + item.label) }}</RouterLink
                >
            </section>
        </template>
        <RouterLink to="/about" class="nav-link small" @click="expanded = false">{{ $t('menu.About') }}</RouterLink>
    </nav>
</template>
<script setup lang="ts">
import { computed, getCurrentInstance, onUnmounted, ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { BIconHouse, BIconList } from 'bootstrap-icons-vue';
import { isLoggedIn, logout } from '@/utils/authentication';
import { mainNavigation } from '@/utils/hemsNavigation';
import LocaleSwitcher from './LocaleSwitcher.vue';
import ThemeSwitcher from './ThemeSwitcher.vue';
const expanded = ref(false),
    isLogged = ref(isLoggedIn());
const route = useRoute(),
    router = useRouter();
const systemOpen = computed(
    () => !['/', '/live'].includes(route.path) && !route.path.startsWith('/plant') && !route.path.startsWith('/energy')
);
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
