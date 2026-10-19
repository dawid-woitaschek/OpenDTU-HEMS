<template>
    <nav :aria-label="$t('hemsUi.navigation')" class="mb-4">
        <ul class="nav nav-pills gap-1">
            <li class="nav-item" v-for="item in items" :key="item.path">
                <RouterLink :to="item.path" class="nav-link px-3" active-class="active">{{
                    $t('hemsUi.' + item.label)
                }}</RouterLink>
            </li>
        </ul>
    </nav>
</template>
<script setup lang="ts">
import { hemsNavigation as items } from '@/utils/hemsNavigation';
</script>
