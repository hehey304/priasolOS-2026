int8_t write(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent_inode;
    if (!read_inode(request->parent_inode, &parent_inode)) {
        return 2; 
    }
    
    if ((parent_inode.i_mode & EXT2_S_IFDIR) == 0 && parent_inode.i_mode != EXT2_FT_DIR) {
        return 2; 
    }

    if (p4_check_duplicate(&parent_inode, request->name, request->name_len)) {
        return 1; 
    }

    uint32_t new_inode_num = p4_find_free_inode();
    if (new_inode_num == 0) return -1; 

    struct EXT2Inode new_inode = {0};

    if (request->is_directory || request->buffer_size == 0) {
        new_inode.i_mode = EXT2_FT_DIR; 
        new_inode.i_size = BLOCK_SIZE;
        new_inode.i_blocks = 1;
        
        uint32_t new_dir_block = p4_find_free_block();
        if (new_dir_block == 0) return -1;
        new_inode.i_block[0] = new_dir_block;

        struct BlockBuffer dir_buf = {0}; 
        
        struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *) dir_buf.buf;
        dot->inode = new_inode_num;
        dot->file_type = EXT2_FT_DIR;
        dot->name_len = 1;
        dot->rec_len = 12; 
        get_entry_name(dot)[0] = '.';

        struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *) (dir_buf.buf + dot->rec_len);
        dotdot->inode = request->parent_inode;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name_len = 2;
        dotdot->rec_len = BLOCK_SIZE - dot->rec_len; 
        get_entry_name(dotdot)[0] = '.';
        get_entry_name(dotdot)[1] = '.';

        write_blocks(&dir_buf, new_dir_block, 1);
        
        uint32_t bgd_idx = inode_to_bgd(new_inode_num);
        bgd_table.table[bgd_idx].bg_used_dirs_count++;
        
    } else {
        new_inode.i_mode = EXT2_FT_REG_FILE; 
        new_inode.i_size = request->buffer_size;
        new_inode.i_blocks = 0; // Dikosongkan agar dihitung dinamis oleh helper
        
        uint32_t blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_needed == 0) blocks_needed = 1; 

        uint8_t *data_ptr = (uint8_t *)request->buf;
        for (uint32_t i = 0; i < blocks_needed; i++) {
            // Panggil helper cerdas pengganti limit 12 blok
            uint32_t new_data_block = p4_map_and_allocate_block(&new_inode, i);
            if (new_data_block == 0) return -1; 
            
            struct BlockBuffer data_buf = {0}; 
            
            uint32_t remaining = request->buffer_size > (i * BLOCK_SIZE) ? request->buffer_size - (i * BLOCK_SIZE) : 0;
            uint32_t copy_size = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            
            if (copy_size > 0 && data_ptr != NULL) {
                memcpy(data_buf.buf, data_ptr + (i * BLOCK_SIZE), copy_size);
            }
            write_blocks(&data_buf, new_data_block, 1);
        }
    }

    p4_write_inode(new_inode_num, &new_inode);

    bool entry_added = false;
    uint16_t required_len = sizeof(struct EXT2DirectoryEntry) + request->name_len;
    if (required_len % 4 != 0) required_len += 4 - (required_len % 4); 

    struct BlockBuffer p_dir_buf;
    for (uint32_t i = 0; i < 12; i++) {
        uint32_t p_lba = parent_inode.i_block[i];
        if (p_lba == 0) {
            uint32_t new_parent_block = p4_find_free_block();
            if (new_parent_block == 0) return -1;
            parent_inode.i_block[i] = new_parent_block;
            parent_inode.i_blocks++;
            
            struct BlockBuffer new_p_dir_buf = {0};
            struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry *)new_p_dir_buf.buf;
            new_entry->inode = new_inode_num;
            new_entry->name_len = request->name_len;
            new_entry->file_type = (request->is_directory || request->buffer_size == 0) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
            new_entry->rec_len = BLOCK_SIZE; 
            memset(get_entry_name(new_entry), 0, required_len - sizeof(struct EXT2DirectoryEntry)); 
            memcpy(get_entry_name(new_entry), request->name, request->name_len);
            
            write_blocks(&new_p_dir_buf, new_parent_block, 1);
            entry_added = true;
            break; 
        }

        read_blocks(&p_dir_buf, p_lba, 1);
        uint32_t offset = 0;
        
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(p_dir_buf.buf + offset);
            
            if (entry->rec_len == 0) break;

            uint16_t ideal_len = sizeof(struct EXT2DirectoryEntry) + entry->name_len;
            if (ideal_len % 4 != 0) ideal_len += 4 - (ideal_len % 4);

            if ((entry->inode == 0 && entry->rec_len >= required_len) || (entry->inode != 0 && entry->rec_len >= ideal_len + required_len)) {
                uint16_t old_rec_len = entry->rec_len;
                
                if (entry->inode != 0) {
                    entry->rec_len = ideal_len; 
                    entry = (struct EXT2DirectoryEntry *)((uint8_t *)entry + ideal_len); 
                    entry->rec_len = old_rec_len - ideal_len; 
                } else {
                    entry->rec_len = old_rec_len; 
                }

                entry->inode = new_inode_num;
                entry->name_len = request->name_len;
                entry->file_type = (request->is_directory || request->buffer_size == 0) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
                
                memset((char*)entry + sizeof(struct EXT2DirectoryEntry), 0, required_len - sizeof(struct EXT2DirectoryEntry));
                memcpy((char*)entry + sizeof(struct EXT2DirectoryEntry), request->name, request->name_len);

                write_blocks(&p_dir_buf, p_lba, 1);
                entry_added = true;
                break; 
            }
            offset += entry->rec_len;
        }
        if (entry_added) break; 
    }

    if (!entry_added) return -1; 

    struct BlockBuffer bgd_buf;
    memcpy(bgd_buf.buf, &bgd_table, sizeof(bgd_table));
    write_blocks(&bgd_buf, 2, 1);
    
    p4_write_inode(request->parent_inode, &parent_inode);

    return 0; 
}