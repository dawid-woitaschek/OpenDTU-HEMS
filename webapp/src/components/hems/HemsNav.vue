<template>
    <nav :aria-label="$t('hemsUi.navigation')" class="mb-4">
        <ul class="nav nav-pills gap-1">
            <li class="nav-item" v-for="item in items" :key="item.path">
                <RouterLink
                    :to="item.path"
                    class="nav-link px-3"
                    :class="{ active: $route.path === item.path }"
                    :aria-current="$route.path === item.path ? 'page' : undefined"
                    >{{ $t('hemsUi.' + item.label) }}</RouterLink
                >
            </li>
        </ul>
    </nav>
</template>
<script setup lang="ts">
const items = [
    { path: '/hems', label: 'overview' },
    { path: '/hems/settings', label: 'settings' },
    { path: '/hems/devices', label: 'devices' },
    { path: '/hems/diagnostics', label: 'diagnostics' },
];
</script>
