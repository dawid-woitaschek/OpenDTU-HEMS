<template>
    <NavBar />
    <div id="main-content" class="app-content" tabindex="-1"><router-view /></div>
</template>
<script setup lang="ts">
import NavBar from './components/NavBar.vue';
</script>
