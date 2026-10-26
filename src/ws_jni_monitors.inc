/* Included inside ws_jni_vm.c. The VM mutex protects ownership. Each enter
 * pins the object, including while blocked, so reference deletion cannot free
 * a live monitor. Waiting releases and restores the entire recursion depth. */
static jint monitor_error(environment *e,const char *kind,const char *message) {
    failure(e,kind,message);return JNI_ERR;
}
static jint jni_MonitorEnter(JNIEnv *env,jobject handle) {
    environment *e=state(env);pthread_mutex_lock(&e->vm->mutex);
    object *o=find_object(e->vm,handle);jint result=JNI_ERR;
    if(!o){monitor_error(e,"java/lang/NullPointerException","MonitorEnter");goto done;}
    if(!o->monitor){
        monitor *m=calloc(1,sizeof(*m));
        if(!m){monitor_error(e,"java/lang/OutOfMemoryError","monitor");goto done;}
        int error=pthread_cond_init(&m->enter,NULL);
        if(!error){error=pthread_cond_init(&m->notify,NULL);if(error)pthread_cond_destroy(&m->enter);}
        if(error){free(m);monitor_error(e,"java/lang/IllegalStateException","monitor condition initialization");goto done;}
        o->monitor=m;
    }
    monitor *m=o->monitor;
    if(o->refs==UINT_MAX||m->depth==UINT_MAX){monitor_error(e,"java/lang/IllegalStateException","monitor recursion limit");goto done;}
    ++o->refs;
    while(m->owner&&m->owner!=e){
        if(pthread_cond_wait(&m->enter,&e->vm->mutex)){
            monitor_error(e,"java/lang/IllegalStateException","monitor entry wait");release(e->vm,o);goto done;
        }
    }
    m->owner=e;++m->depth;result=JNI_OK;
done:
    pthread_mutex_unlock(&e->vm->mutex);return result;
}
static jint jni_MonitorExit(JNIEnv *env,jobject handle) {
    environment *e=state(env);pthread_mutex_lock(&e->vm->mutex);
    object *o=find_object(e->vm,handle);monitor *m=o?o->monitor:NULL;jint result=JNI_ERR;
    if(!o)monitor_error(e,"java/lang/NullPointerException","MonitorExit");
    else if(!m||m->owner!=e||!m->depth)monitor_error(e,"java/lang/IllegalMonitorStateException","MonitorExit without ownership");
    else {
        if(!--m->depth){m->owner=NULL;pthread_cond_broadcast(&m->enter);}
        release(e->vm,o);result=JNI_OK;
    }
    pthread_mutex_unlock(&e->vm->mutex);return result;
}
int ws_jni_monitor_wait(JNIEnv *env,jobject handle,jlong milliseconds,jint nanos) {
    environment *e=state(env);pthread_mutex_lock(&e->vm->mutex);int result=JNI_ERR;
    object *o=find_object(e->vm,handle);monitor *m=o?o->monitor:NULL;
    if(milliseconds<0||nanos<0||nanos>999999){monitor_error(e,"java/lang/IllegalArgumentException","Object.wait timeout");goto done;}
    if(!m||m->owner!=e||!m->depth){monitor_error(e,"java/lang/IllegalMonitorStateException","Object.wait without ownership");goto done;}
    int timed=milliseconds||nanos;struct timespec deadline={0};
    if(timed){
        if(clock_gettime(CLOCK_REALTIME,&deadline)){monitor_error(e,"java/lang/IllegalStateException","Object.wait clock");goto done;}
        /* Java's (millis,nanos) overload rounds a nonzero nanos part up to ms. */
        uint64_t duration=(uint64_t)milliseconds+(nanos?1u:0u);
        uint64_t seconds=(uint64_t)deadline.tv_sec+duration/1000;
        long nanoseconds=deadline.tv_nsec+(long)(duration%1000)*1000000L;
        if(nanoseconds>=1000000000L){++seconds;nanoseconds-=1000000000L;}
        uint64_t maximum=sizeof(time_t)>=8?(uint64_t)INT64_MAX:INT32_MAX;
        deadline.tv_sec=(time_t)(seconds>maximum?maximum:seconds);deadline.tv_nsec=nanoseconds;
    }
    unsigned depth=m->depth;m->depth=0;m->owner=NULL;pthread_cond_broadcast(&m->enter);
    int error=timed?pthread_cond_timedwait(&m->notify,&e->vm->mutex,&deadline):pthread_cond_wait(&m->notify,&e->vm->mutex);
    /* Preserve ownership even on a timeout or spurious wakeup. A failure in
     * this internal condition variable cannot safely return without it. */
    while(m->owner&&m->owner!=e)if(pthread_cond_wait(&m->enter,&e->vm->mutex))abort();
    m->owner=e;m->depth=depth;
    if(error&&error!=ETIMEDOUT)monitor_error(e,"java/lang/IllegalStateException","Object.wait condition");
    else result=JNI_OK;
done:
    pthread_mutex_unlock(&e->vm->mutex);return result;
}
int ws_jni_monitor_notify(JNIEnv *env,jobject handle,int all) {
    environment *e=state(env);pthread_mutex_lock(&e->vm->mutex);int result=JNI_ERR;
    object *o=find_object(e->vm,handle);monitor *m=o?o->monitor:NULL;
    if(!m||m->owner!=e||!m->depth)monitor_error(e,"java/lang/IllegalMonitorStateException","Object.notify without ownership");
    else {
        int error=all?pthread_cond_broadcast(&m->notify):pthread_cond_signal(&m->notify);
        if(error)monitor_error(e,"java/lang/IllegalStateException","Object.notify condition");
        else result=JNI_OK;
    }
    pthread_mutex_unlock(&e->vm->mutex);return result;
}
static void monitor_release_owned(environment *e) {
    /* JNI requires automatic release at detach. Restart after each removal:
     * releasing an object's fields can also release another list element. */
    for(;;){
        object *o=e->vm->objects;
        while(o&&(!o->monitor||o->monitor->owner!=e))o=o->next;
        if(!o)break;
        monitor *m=o->monitor;unsigned depth=m->depth;m->owner=NULL;m->depth=0;
        pthread_cond_broadcast(&m->enter);o->refs-=depth-1;release(e->vm,o);
    }
}
